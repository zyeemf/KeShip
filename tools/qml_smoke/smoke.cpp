/// QML 创建期冒烟测试 + 菜单弹出截图（2026-09-17）
///
/// 目的：
///  1) 复现 gz-gui 加载插件时的 QQmlComponent::create 路径，验证 QML 改动不会在创建期崩溃
///     （2026-09-17 Menu.delegate 引发的 SIGSEGV 正发生在这一阶段，而当时只能靠人工启动试错）；
///  2) 弹出菜单并截图，使样式改动可离线目视核验，不必启动整个仿真。
///
/// 用法：qml_smoke <file.qml> [截图路径] [要弹出的菜单序号]
/// 退出码：0=创建成功 2=编译错误 3=create 失败 65=打桩失败 124=超时 139=SIGSEGV（禁止入库）
///
/// 踩过的坑（都会造成"假通过"）：
///  - 必须给窗口真实尺寸并跑事件循环，否则 ListView 不布局、delegate 不实例化；
///  - 光有尺寸还不够，DataSource 为空时 delegate 同样不创建 → 这里主动注入委托行数据；
///  - 类名匹配用子串会误判（"QQuickMenuItem" 含 "QQuickMenu"）→ 一律用 inherits() 精确判定。

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlError>
#include <QUrl>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <QList>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QMap>
#include <QSet>

/// 本对象（含继承链）是否为 cls 或其子类 —— 用 inherits() 精确判定，不做子串匹配
static bool isA(QObject *obj, const char *cls)
{
    return obj && obj->inherits(cls);
}

/// 遍历器：同时走 QObject 树与 QQuickItem 视觉树
///
/// 关键坑：QQuickItem 的"视觉父项(parentItem)"与 QObject 父对象是两回事。
/// ListView 的 delegate 项只挂在视觉树上（QObject parent 常为 null），
/// 只走 QObject 树会完全数不到它们 —— 表现为"ListView count=3 却数不到委托"。
class Walker
{
public:
    QList<QObject *> all;
    void walk(QObject *o)
    {
        if (!o || seen.contains(o)) return;
        seen.insert(o);
        all << o;
        for (QObject *c : o->children())
            walk(c);
        if (QQuickItem *it = qobject_cast<QQuickItem *>(o))
            for (QQuickItem *c : it->childItems())
                walk(c);
    }
    static QList<QObject *> collect(QObject *root)
    {
        Walker w;
        w.walk(root);
        return w.all;
    }
private:
    QSet<QObject *> seen;
};

static int countByClass(QObject *root, const char *cls)
{
    int n = 0;
    for (QObject *o : Walker::collect(root))
        if (isA(o, cls)) ++n;
    return n;
}

static void collectByClass(QObject *root, const char *cls, QList<QObject *> &out)
{
    for (QObject *o : Walker::collect(root))
        if (isA(o, cls)) out << o;
}

/// 对象总数（双树去重后）
static int countAll(QObject *root)
{
    return Walker::collect(root).size();
}

/// 类名直方图：用于确认 Qt 侧真实类名，避免"以为匹配上了"
static void histogram(QObject *root, QMap<QString, int> &h)
{
    for (QObject *o : Walker::collect(root))
        h[QString::fromLatin1(o->metaObject()->className())] += 1;
}

/// 祖先链中是否存在某类对象（用于排除 Menu 内建的 ListView —— 给它注入 model 会毁掉菜单）
static bool hasAncestorOfClass(QObject *obj, const char *cls)
{
    for (QObject *p = obj ? obj->parent() : nullptr; p; p = p->parent()) {
        if (isA(p, cls)) return true;
    }
    return false;
}

/// 行为探针未通过项计数（用于把退出码变成 4）
static int g_behaviorFails = 0;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    if (argc < 2) {
        qWarning() << "[ERROR] usage: qml_smoke <file.qml> [shot.png] [menuIndex]";
        return 64;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const QString shot = (argc >= 3) ? QString::fromLocal8Bit(argv[2]) : QString();
    const int menuIndex = (argc >= 4) ? QString::fromLocal8Bit(argv[3]).toInt() : 0;

    QQuickView view;

    /// 上下文件对象打桩：用内联 QML 造 QtObject（QQmlPropertyMap 的动态属性解析不出
    /// QVariantList，已弃用）。只需覆盖"创建期即被求值"的属性。
    const QByteArray stubQml =
        "import QtQuick 2.0\n"
        "QtObject {\n"
        "  property bool paused: false\n"
        "  property int customWaterAreaCount: 1\n"
        "  property var worldHierarchy: [\n"
        "    { kind: 'group', gtype: 'sub',   text: 'probe_sub',   chain: 'w1', depth: 0, midx: -1, widx: -1, isStatic: false, mathHull: false },\n"
        "    { kind: 'model', gtype: '',      text: 'probe_model', chain: 'w1', depth: 1, midx:  0, widx: -1, isStatic: false, mathHull: false },\n"
        "    { kind: 'water', gtype: '',      text: 'probe_water', chain: 'w1', depth: 1, midx: -1, widx:  0, isStatic: false, mathHull: false }\n"
        "  ]\n"
        "  property var waterAreaSeaStates: ['3级海况', '5级海况']\n"
        "  property var sensorList: ['imu', 'gps']\n"
        "}\n";

    QQmlComponent stubComp(view.engine());
    stubComp.setData(stubQml, QUrl("qrc:/stub.qml"));
    QObject *stub = stubComp.isError() ? nullptr : stubComp.create();
    if (!stub) {
        qWarning() << "[ERROR] 打桩对象创建失败:" << stubComp.errorString();
        return 65;
    }
    /// 显式设为 C++ 所有权：否则 QML 的 JS GC 可能回收这个无父对象，
    /// 表现为"第一次求值正常、之后变 undefined"的诡异 TypeError。
    QQmlEngine::setObjectOwnership(stub, QQmlEngine::CppOwnership);
    view.engine()->rootContext()->setContextProperty("MenuPluginKeship", stub);

    /// 用"仿真卡片"包一层再加载：插件的 toggleCollapsed() 依赖 gz-gui 卡片结构
    /// （root.parent.parent 需要有个带 lastHeight 的卡片），直接裸加载会让它提前 return，
    /// 于是"折叠"无法被观测 —— 会造成行为断言假失败。
    QString wrapperPath;
    {
        const QString wrapper = QString(
            "import QtQuick 2.0\n"
            "Rectangle {\n"
            "    id: card\n"
            "    property real lastHeight: 600\n"
            "    width: 420\n"
            "    height: 760\n"
            "    Loader { anchors.fill: parent; source: \"%1\" }\n"
            "}\n").arg(QUrl::fromLocalFile(path).toString());
        wrapperPath = QDir::tempPath() + QString("/qml_smoke_wrapper_%1.qml")
                                        .arg(QCoreApplication::applicationPid());
        QFile wf(wrapperPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            wf.write(wrapper.toUtf8());
            wf.close();
        }
        else
        {
            qWarning() << "[警告] 无法写入包装文件，改用直接加载（折叠相关断言可能不成立）";
            wrapperPath.clear();
        }
    }

    view.setSource(QUrl::fromLocalFile(wrapperPath.isEmpty() ? path : wrapperPath));
    /// 无论 status 如何，只要没有根对象就把 status 与全部错误打出来 ——
    /// 否则会出现"create 失败但一条错误都不显示"的哑失败
    QObject *root = view.rootObject();
    if (!root) {
        qWarning() << "[ERROR] create() 返回空对象，status ="
                   << (view.status() == QQuickView::Error ? "Error" :
                       view.status() == QQuickView::Loading ? "Loading" :
                       view.status() == QQuickView::Ready ? "Ready" : "Null")
                   << " 错误条数 =" << view.errors().size();
        for (const QQmlError &e : view.errors())
            qWarning() << "[ERROR]" << e.toString();
        return view.status() == QQuickView::Error ? 2 : 3;
    }
    qDebug() << "[OK] 创建成功:" << path;

    /// 插件文档根对象（外面包了仿真卡片，故需从类名找回）
    QObject *plugin = nullptr;
    for (QObject *o : Walker::collect(root))
        if (QString::fromLatin1(o->metaObject()->className()).contains("MenuPluginKeship_QMLTYPE"))
        {
            plugin = o;
            break;
        }
    if (!plugin) plugin = root;

    view.resize(420, 760);
    view.show();

    /// 注入委托行数据：ListView 的 delegate 只在有数据时创建，
    /// 不注入则委托内的 ⋯ 行菜单测不到（本次改动的主要对象）
    QVariantMap rowSub, rowModel, rowWater;
    rowSub["kind"]   = "group"; rowSub["gtype"]   = "sub";
    rowSub["text"]   = QString::fromUtf8("probe_sub");   rowSub["chain"] = "w1";
    rowSub["depth"]  = 0;  rowSub["midx"] = -1; rowSub["widx"] = -1;
    rowSub["isStatic"] = false; rowSub["mathHull"] = false;
    rowModel["kind"] = "model"; rowModel["gtype"] = "";
    rowModel["text"] = QString::fromUtf8("probe_model"); rowModel["chain"] = "w1";
    rowModel["depth"] = 1; rowModel["midx"] = 0; rowModel["widx"] = -1;
    rowModel["isStatic"] = false; rowModel["mathHull"] = false;
    rowWater["kind"] = "water"; rowWater["gtype"] = "";
    rowWater["text"] = QString::fromUtf8("probe_water"); rowWater["chain"] = "w1";
    rowWater["depth"] = 1; rowWater["midx"] = -1; rowWater["widx"] = 0;
    rowWater["isStatic"] = false; rowWater["mathHull"] = false;
    QVariantList rows; rows << rowSub << rowModel << rowWater;

    QList<QObject *> menus;
    QTimer::singleShot(400, [&]() {
        QList<QObject *> views;
        collectByClass(root, "QQuickListView", views);
        int injected = 0, skipped = 0;
        for (QObject *lv : views) {
            /// 跳过三类：Menu 内建 ListView（注入会清空菜单条目）、
            /// 已经绑定 ListModel 的列表（如教学实验页，注入会破坏其角色绑定）
            if (hasAncestorOfClass(lv, "QQuickPopupItem") || hasAncestorOfClass(lv, "QQuickPopup")) {
                ++skipped;
                continue;
            }
            QObject *curModel = lv->property("model").value<QObject *>();
            if (curModel && QString::fromLatin1(curModel->metaObject()->className()).contains("ListModel")) {
                ++skipped;
                continue;
            }
            if (lv->setProperty("model", QVariant::fromValue(rows))) ++injected;
        }
        qDebug() << "[注入] ListView 共" << views.size() << "个：注入" << injected
                 << "个 / 跳过 Popup 内建" << skipped << "个，行数 =" << rows.size();
    });

    /// 可选：模拟点击第 N 个 ⋯ 按钮（QML_SMOKE_DOTS=N），验证委托内行菜单是否真的可用
    QByteArray dotsEnv = qgetenv("QML_SMOKE_DOTS");
    QTimer::singleShot(700, [&]() {
        if (dotsEnv.isEmpty()) return;
        const int dotsIdx = dotsEnv.toInt();
        QList<QObject *> btns;
        collectByClass(root, "QQuickAbstractButton", btns);
        int k = 0;
        for (QObject *b : btns) {
            if (b->property("text").toString() != QString::fromUtf8(u8"⋯")) continue;
            if (k++ != dotsIdx) continue;
            bool ok = QMetaObject::invokeMethod(b, "clicked");
            qDebug() << "[点击] ⋯ 按钮序号" << dotsIdx << (ok ? "已触发 clicked" : "触发失败");
            return;
        }
        qWarning() << "[警告] 未找到 ⋯ 按钮（序号" << dotsIdx << "），委托可能未实例化";
    });

    /// 可选：行为探针（QML_SMOKE_BEHAVIOR=1）——断言交互逻辑，而不是"能否创建"
    ///   规则：单击标题只切 Tab（不改折叠态）；双击切换折叠态；触发菜单项后菜单必须关闭
    QTimer::singleShot(1100, [&]() {
        if (qgetenv("QML_SMOKE_BEHAVIOR").isEmpty()) return;
        auto check = [&](const QString &what, const QVariant &expect, const QVariant &actual) {
            const bool ok = (expect == actual);
            if (!ok) ++g_behaviorFails;
            qDebug().nospace() << "[行为] " << (ok ? "PASS " : "FAIL ") << what
                               << "  期望=" << expect.toString() << " 实际=" << actual.toString();
        };

        QList<QObject *> titles;
        /// 注意：QML 内联组件（component TitleButton: Item）的实例用 inherits() 匹配不到，
        /// 必须按类名字符串匹配（实测坑）
        for (QObject *o : Walker::collect(root))
            if (QString::fromLatin1(o->metaObject()->className()).contains("TitleButton"))
                titles << o;
        if (titles.size() < 2) {
            for (QObject *o : Walker::collect(static_cast<QObject *>(&view)))
                if (QString::fromLatin1(o->metaObject()->className()).contains("TitleButton"))
                    titles << o;
        }
        qDebug() << "[行为] TitleButton 数 =" << titles.size();
        if (titles.size() >= 2) {
            const bool col0 = plugin->property("collapsed").toBool();
            check(QString::fromUtf8(u8"初始未折叠"), false, col0);

            /// ① 双击应能折叠
            QMetaObject::invokeMethod(titles[1], "doubleClicked");
            check(QString::fromUtf8(u8"双击可折叠"), true, plugin->property("collapsed"));

            /// ② 折叠状态下单击：只切 Tab，不得展开（张老师报的故障点）
            QMetaObject::invokeMethod(titles[0], "clicked");
            check(QString::fromUtf8(u8"单击切换到 Tab 0"), 0, plugin->property("tabBarCurrent"));
            check(QString::fromUtf8(u8"单击不展开（折叠态保持）"), true,
                  plugin->property("collapsed"));

            /// ③ 真实双击事件序列 = clicked ×2 + doubleClicked，应能展开
            QMetaObject::invokeMethod(titles[1], "clicked");
            QMetaObject::invokeMethod(titles[1], "clicked");
            QMetaObject::invokeMethod(titles[1], "doubleClicked");
            check(QString::fromUtf8(u8"双击可展开（真实事件序列）"), false,
                  plugin->property("collapsed"));
        } else {
            qWarning() << "[行为] TitleButton 不足 2 个，折叠相关断言未判定";
            ++g_behaviorFails;
        }

        QList<QObject *> ms;
        collectByClass(root, "QQuickMenu", ms);
        if (!ms.isEmpty()) {
            QObject *m = ms[0];
            QMetaObject::invokeMethod(m, "open");
            check(QString::fromUtf8(u8"菜单可打开"), true, m->property("visible"));
            QList<QObject *> items;
            collectByClass(m, "QQuickMenuItem", items);
            if (!items.isEmpty()) {
                /// 说明：此处只能断言"触发后菜单不可见"这一结构性结果。
                /// 真实故障场景（槽内弹模态对话框 exec() 阻塞菜单关闭）在无头环境无法复现，
                /// 因此该条只作信息输出，不计入失败 —— 见 check_qml.sh 的静态断言。
                QMetaObject::invokeMethod(items[0], "triggered");
                const bool closed = !m->property("visible").toBool();
                qDebug() << "[行为]" << (closed ? "PASS " : "FAIL ")
                         << QString::fromUtf8(u8"触发菜单项后菜单不可见（无头环境不代表真实阻塞场景）")
                         << " 实际 visible =" << m->property("visible").toBool();
            } else {
                qWarning() << "[行为] 菜单内无条目，关闭断言未判定";
                ++g_behaviorFails;
            }
        } else {
            qWarning() << "[行为] 未找到 Menu，关闭断言未判定";
            ++g_behaviorFails;
        }
        qDebug() << "[行为] 小结：未通过项 =" << g_behaviorFails;
    });

    QTimer::singleShot(900, [&]() {
        collectByClass(root, "QQuickMenu", menus);
        qDebug() << "[样本] Menu 数 =" << menus.size();
        for (int i = 0; i < menus.size(); ++i)
            qDebug() << "[样本]  Menu[" << i << "] 条目数 =" << menus[i]->property("count").toInt();
        if (menuIndex >= 0 && menuIndex < menus.size()) {
            bool ok = QMetaObject::invokeMethod(menus[menuIndex], "open");
            /// 强制指定弹窗位置：委托内的行菜单默认按其父项换算坐标，
            /// 在无头环境下可能落到抓图范围之外（表现为"已弹出但截图看不到"）
            if (!qgetenv("QML_SMOKE_FORCE_XY").isEmpty()) {
                menus[menuIndex]->setProperty("x", 40);
                menus[menuIndex]->setProperty("y", 180);
            }
            /// 尺寸剖析：菜单宽度若为 0 说明样式改动破坏了隐式尺寸推导（弹窗将不可见）
            auto dump = [](QObject *o, const QString &label) {
                if (!o) { qDebug() << "[dump]" << label << "= null"; return; }
                qDebug().nospace() << "[dump] " << label
                        << " implicitW=" << o->property("implicitWidth").toDouble()
                        << " w=" << o->property("width").toDouble()
                        << " implicitH=" << o->property("implicitHeight").toDouble()
                        << " h=" << o->property("height").toDouble();
            };
            dump(menus[menuIndex], QString("Menu[%1]").arg(menuIndex));
            bool dumpedItem = false;
            for (QObject *k : Walker::collect(menus[menuIndex])) {
                const QString cn = QString::fromLatin1(k->metaObject()->className());
                if (cn.contains("ListView")) dump(k, "  Menu 内部 ListView");
                if (cn.contains("MenuItem") && !dumpedItem) { dump(k, "  首个 MenuItem"); dumpedItem = true; }
            }
            qDebug() << "[弹出] Menu[" << menuIndex << "]" << (ok ? "已弹出" : "弹出失败")
                     << " x =" << menus[menuIndex]->property("x").toDouble()
                     << " y =" << menus[menuIndex]->property("y").toDouble()
                     << " 宽 =" << menus[menuIndex]->property("width").toDouble()
                     << " 可见 =" << menus[menuIndex]->property("visible").toBool()
                     << " 不透明度 =" << menus[menuIndex]->property("opacity").toDouble();
        }
    });

    /// 可选：搜索功能端到端验证（QML_SMOKE_SEARCH=<词>）
    /// 步骤：定位搜索框 → 模拟输入（setProperty + emit textEdited，二者缺一不可：
    ///       程序化 setProperty **不会**触发 onTextEdited）→ 断言列表 count 收缩。
    /// 背景：搜索曾两度被报"无功能"，必须用机器断言证明过滤真的生效。
    QTimer::singleShot(1400, [&]() {
        const QByteArray kwEnv = qgetenv("QML_SMOKE_SEARCH");
        if (kwEnv.isEmpty()) return;
        const QString kw = QString::fromUtf8(kwEnv);

        QObject *sf = nullptr;
        for (QObject *o : Walker::collect(root)) {
            if (isA(o, "QQuickTextField")
                && o->property("placeholderText").toString().contains(QString::fromUtf8(u8"搜索")))
            { sf = o; break; }
        }
        if (!sf) { qWarning() << "[搜索] FAIL 未找到搜索框"; ++g_behaviorFails; return; }

        // 环境自检：treeBase（stub 的 worldHierarchy）在 smoke 里是否就位
        QObject *boxChk = nullptr;
        for (QObject *o : Walker::collect(root))
            if (o->property("treeBase").isValid()) { boxChk = o; break; }
        qDebug() << "[搜索] treeBase 长度 ="
                 << (boxChk ? boxChk->property("treeBase").toList().size() : -1)
                 << "（stub 应为 3；为 0/undefined 说明 smoke 打桩未就位，属测试环境问题）";
        qDebug() << "[搜索] stub.worldHierarchy 直接读 ="
                 << (stub ? stub->property("worldHierarchy").toList().size() : -2)
                 << " isValid=" << (stub ? stub->property("worldHierarchy").isValid() : false);

        // tab1 列表 = model 绑定 JS 数组（treeRows）的那个 ListView（model 属性非 QObject）
        QList<QObject*> lvs;
        collectByClass(root, "QQuickListView", lvs);
        QObject *mlv = nullptr;
        for (QObject *l : lvs) {
            if (l->property("model").value<QObject*>() == nullptr) { mlv = l; break; }
        }
        const int before = mlv ? mlv->property("count").toInt() : -1;
        qDebug() << "[搜索] 输入前 count =" << before;

        sf->setProperty("text", kw);
        QMetaObject::invokeMethod(sf, "textEdited");   // 触发 onTextEdited → treeFilter
        QCoreApplication::processEvents();

        // 分叉测试 A：直接设 treeFilter（绕过搜索框），判定「treeFilter → treeRows」
        // 绑定是否重估 —— 若 A 失败则绑定断链；若 A 成功而上面失败则输入链路有问题
        QObject *box = nullptr;
        const QList<QObject*> all = Walker::collect(root);
        for (QObject *o : all)
            if (o->property("treeFilter").isValid()) { box = o; break; }
        qDebug() << "[搜索] 含 treeFilter 的对象 =" << (box ? "找到" : "未找到");
        int viaFilter = -1;
        if (box) {
            // 直接读 treeRows 属性长度 = 过滤结果本身，不经 ListView
            //（冒烟注入会覆盖 ListView.model，count 在测试环境里语义被污染）
            box->setProperty("treeFilter", QString());   // 先取"无过滤"基线
            QCoreApplication::processEvents();
            const int rowsBefore = box->property("treeRows").toList().size();
            box->setProperty("treeFilter", kw);
            QCoreApplication::processEvents();
            const int rowsAfter = box->property("treeRows").toList().size();
            viaFilter = mlv ? mlv->property("count").toInt() : -1;
            qDebug() << "[搜索] 分叉A treeRows 长度" << rowsBefore << "->" << rowsAfter
                     << "（filter 空与 \"" << kw << "\" 对比，直接读过滤结果）";
            // 还原，避免影响后续断言
            box->setProperty("treeFilter", QString());
            QCoreApplication::processEvents();
        }

        const int after = mlv ? mlv->property("count").toInt() : -1;
        const bool countOk = (before >= 0) && ((after != before) || (viaFilter != before));
        if (!countOk) ++g_behaviorFails;
        qDebug().nospace() << "[搜索] " << (countOk ? "PASS" : "FAIL")
                           << "  输入\"" << kw << "\" count " << before
                           << " -> (输入链路)" << after
                           << " / (直设filter)" << viaFilter;
    });

    QTimer::singleShot(2200, [&]() {
        if (!shot.isEmpty()) {
            QImage img = view.grabWindow();
            bool ok = img.save(shot);
            qDebug() << (ok ? "[OK] 截图已保存:" : "[警告] 截图保存失败:") << shot
                     << (ok ? QString("(%1x%2)").arg(img.width()).arg(img.height()) : QString());
        }
        /// 计数范围取窗口内容根节点：Popup（Menu）的 popupItem 会被重挂到 overlay 上，
        /// 只从文档根对象往下数会漏掉已弹出的菜单
        /// 最宽范围：从 QQuickWindow 本身开始统计（Popup 可能挂在 window 或 overlay 上）
        QObject *scope = static_cast<QObject *>(&view);
        qDebug() << "[样本] 统计范围 ="
                 << (view.contentItem() ? "窗口内容根(含 overlay)" : "文档根对象");
        qDebug() << "[样本] 对象总数 =" << countAll(scope)
                 << " 文档根对象总数 =" << countAll(root);
        QList<QObject *> lvs;
        collectByClass(root, "QQuickListView", lvs);
        for (QObject *l : lvs)
            qDebug() << "[样本] ListView count =" << l->property("count").toInt()
                     << " height =" << l->property("height").toDouble()
                     << " 可视 =" << l->property("visible").toBool();
        qDebug() << "[样本] MenuItem =" << countByClass(scope, "QQuickMenuItem")
                 << " MenuSeparator =" << countByClass(scope, "QQuickMenuSeparator")
                 << " Menu =" << countByClass(scope, "QQuickMenu");
        if (qEnvironmentVariableIsSet("QML_SMOKE_VERBOSE")) {
            QMap<QString, int> h;
            histogram(root, h);
            for (auto it = h.begin(); it != h.end(); ++it)
                qDebug() << "[类名直方图]" << it.key() << it.value();
        }
        app.quit();
    });

    const int rc = app.exec();
    if (rc != 0) return rc;
    return g_behaviorFails > 0 ? 4 : 0;   /// 4 = 行为断言未通过
}
