/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-02 型线图识读 —— 识图训练实验对话框
 *
 * 教学定位（与 SD-08 的分工）：
 *   SD-08 = **设计**（改 L/B/D 与形状指数，生成型线、反解 Cb）；
 *   SD-02 = **识读**（拿到一份既成型线图，能否读出指定位置的半宽/站位/水线）。
 *   本实验**不造新船**，只训练"三视图 ↔ 型值表"的双向读图能力。
 *
 * 型线来源二选一：
 *   （a）按 L/B/D 现场生成（ShipHullGenerator，Wigley 经典）；
 *   （b）**导入 SD-08 型线**（`sd08_lines.json`，expId 必须为 "SD-08"，串号拒绝），
 *        读 extra.stations / waterlines / halfBreadths。
 *
 * 识读练习：程序随机抽 5 题，三种题型轮转 ——
 *   ① 给站位 + 水线 → 求半宽（容差 ±0.5%）
 *   ② 给站位 + 半宽   → 求所在水线（序号，精确匹配）
 *   ③ 给空间点        → 判断是否在船体内（布尔）
 * 判分标准答案**一律从 LinesData 现算**（HalfBreadthAt / 直接查表），
 * 绝不写死常量 —— 否则换船型后答案就假了。
 *
 * 交付物 `sd02_lines_reading.json`（schema keship.exp.v1，expId = "SD-02"）：
 *   metrics：score / totalQuestions / accuracy / nStations / nWaterlines / maxHalfBreadthM
 *   extra.questions：逐题 {type, stationIdx, waterlineIdx, studentAnswer, correctAnswer, passed}
 *   本实验是**非链**实验（认知类），不产出被下游引用的交付物，但仍写成同一 schema
 *   以便统一归档与查看器解析。
 */

#ifndef LINES_READING_DIALOG_HH_
#define LINES_READING_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "ShipHullGenerator.hh"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace keship {
namespace sd02 {

/// 题型
enum class QType {
  kHalfBreadth = 0,  ///< 给站位+水线 → 求半宽
  kWaterline = 1,    ///< 给站位+半宽 → 求所在水线（序号）
  kInside = 2        ///< 给空间点 → 判断是否在船体内
};

/// 一道识读题。correct* 在抽题时由型值表现算；判定时**再算一次**（JudgeQuestion），
/// 保证"标准答案"永远跟着数据走，不会因为界面缓存而漂移。
struct Question {
  QType type = QType::kHalfBreadth;
  int stationIdx = -1;
  int waterlineIdx = -1;  ///< 判断题：参考水线（最近一条）；其余题：题面所指水线

  // 判断题的取样点 / 水线题的已知半宽
  double probeX = 0.0, probeY = 0.0, probeZ = 0.0;
  double targetHalfBreadth = 0.0;

  double correctNumber = 0.0;  ///< 数值题标准答案（半宽 m / 水线序号）
  bool correctBool = false;    ///< 判断题标准答案
  double studentNumber = 0.0;
  bool studentBool = false;
  /// 学生是否真的作答（未作答的题不得静默按"否"判分；交付物里记 null）
  bool answered = true;
  bool passed = false;

  bool numeric() const { return type != QType::kInside; }
};

/// 型值表双线性插值半宽（x 沿船长、z 沿吃水；水线 z 可升可降，按表内实际方向插值）
double HalfBreadthAt(const LinesData &_ld, double _x, double _z);

/// 最近水线索引（|z − waterlines[j]| 最小）
int WaterlineIndexAt(const LinesData &_ld, double _z);

/// 随机抽 _n 题（_seed 相同则完全可复现；前 3 题保证覆盖三种题型）
std::vector<Question> GenerateQuestions(const LinesData &_ld, int _n, unsigned _seed);

/// 按 _q.student* 判定；标准答案在此**从型值表现算并写回** _q.correct*。
/// _tolPct 只作用于"求半宽"题（相对容差，%）；水线序号精确匹配。
bool JudgeQuestion(Question &_q, const LinesData &_ld, double _tolPct);

int ScoreOf(const std::vector<Question> &_qs);

QJsonObject QuestionToJson(const Question &_q);

/// 题面文字（含站位 x、水线 z 等已知量，学生据此查图）
QString QuestionPrompt(const Question &_q, const LinesData &_ld);

QString TypeName(const Question &_q);

}  // namespace sd02
}  // namespace keship

/// 三视图（横剖线图 / 半宽水线图 / 纵剖线图）—— QPainter 自绘，简化到能看清
class LinesReadingView : public QWidget
{
  Q_OBJECT

public:
  explicit LinesReadingView(QWidget *_parent = nullptr);

  void setLines(const keship::LinesData &_lines);
  /// 高亮：站位竖线（<0 不高亮）、水线（<0 不高亮）。只高亮**题面已知量**，
  /// 绝不高亮"答案"所在的那条线，否则练习就失去意义。
  void setHighlight(int _stationIdx, int _waterlineIdx);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  void paintBodyPlan(QPainter &_p, const QRectF &_r) const;
  void paintHalfBreadth(QPainter &_p, const QRectF &_r) const;
  void paintProfile(QPainter &_p, const QRectF &_r) const;

  keship::LinesData lines_;
  bool hasData_ = false;
  int hlStation_ = -1;
  int hlWaterline_ = -1;
};

class LinesReadingDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LinesReadingDialog(QWidget *_parent = nullptr);

  /// 无交互导入 SD-08 型线（离屏测试用；失败时 errMsg() 给原因，不弹窗）
  bool importSd08(const QString &_path);
  /// 无交互导出（离屏测试用）：先按当前作答判定，再写交付物
  bool exportTo(const QString &_path);
  QString errMsg() const { return errMsg_; }

  /// 供接线/离屏测试读取
  int score() const;
  int totalQuestions() const { return static_cast<int>(questions_.size()); }
  const std::vector<keship::sd02::Question> &questions() const { return questions_; }
  QString lineSource() const { return lineSource_; }

private slots:
  void onSourceChanged(int _index);
  void onImportSd08();
  void onRegenerate();
  void onNewQuiz();
  void onSubmit();
  void onExport();

private:
  void buildUi();
  /// 按当前来源重建型线（生成 or 已导入），再出一组新题
  bool refresh();
  void buildQuiz();
  void fillResultCells();
  void updateCache();

  // === 输入 ===
  QComboBox *sourceCombo_ = nullptr;
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;

  // === 输出 ===
  LinesReadingView *view_ = nullptr;
  QTableWidget *quizTable_ = nullptr;
  QLabel *promptLabel_ = nullptr;
  QLabel *scoreLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  // === 状态 ===
  keship::LinesData lines_;
  QString lineSource_ = "generated";  ///< "generated" / "imported"
  bool hasImported_ = false;
  QJsonObject importedObject_;
  std::vector<keship::sd02::Question> questions_;
  QJsonObject lastObject_, lastParams_, lastMetrics_, lastExtra_;

  /// 每题一个作答控件：数值题用 QLineEdit，判断题用 QComboBox（是/否）
  std::vector<QLineEdit *> numEdits_;
  std::vector<QComboBox *> boolCombos_;
  unsigned seed_ = 0;

  QString errMsg_;
};

#endif  // LINES_READING_DIALOG_HH_
