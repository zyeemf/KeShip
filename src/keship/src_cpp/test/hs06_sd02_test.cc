/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-06 大倾角稳性 + SD-02 型线图识读 —— 离屏测试
 *
 * 两项都是**非链实验**（认知/原理验证类），不产出被下游引用的交付物，
 * 但"能点开、能操作、能导出机器可读交付物、有离屏测试"这四条同样要求。
 *
 * 覆盖：
 *   HS-06
 *     ① φ=0 时数值 GZ 与 wall-sided 解析 GZ 之差 < 1e-9（两法同一船型必须重合）
 *     ② 5° 处相对偏差 < 1%；5/10/20/30° 偏差单调不减（公式失效是单调恶化）
 *     ③ 舭部出水角 = atan(2T/B)、甲板入水角 = atan(2(D−T)/B)，与手算差 < 1e-6
 *     ④ 交付物写→读→Valid(…,"HS-06") 往返一致；用 "SD-02" 校验必须失败（负例）
 *   SD-02
 *     ⑤ 随机题的标准答案与直接查表逐位一致（半宽 / 水线序号 / 内外判定）
 *     ⑥ 答错判 false、答对判 true（数值题与判断题各一组）
 *     ⑦ score 与逐题 passed 的计数一致
 *
 * 依赖 QT_QPA_PLATFORM=offscreen：本程序自行设置，无需外部 DISPLAY。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "../gui/keship/experiments/LargeAngleStabilityDialog.hh"
#include "../gui/keship/experiments/LinesReadingDialog.hh"
#include "../gui/keship/shared/ExpDeliverable.hh"
#include "ShipHullGenerator.hh"
#include "StabilityGzCore.hh"

namespace {

int gFail = 0;

void Check(bool _cond, const std::string &_msg)
{
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

constexpr double kPi = 3.14159265358979323846;

double Rad2Deg(double _r) { return _r * 180.0 / kPi; }

/// 从采样表中取指定角度的偏差
double DevAt(const keship::hs06::SampleTable &_t, double _phi)
{
  for (const auto &r : _t.rows) {
    if (std::abs(r.phiDeg - _phi) < 1e-6) return r.devPct;
  }
  return -1.0;
}

}  // namespace

int main(int argc, char **argv)
{
  // 离屏：测试机上通常没有 X / Wayland，必须显式指定 offscreen 平台插件
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  // ═════════════════════════════════════════════════════════════════════
  // HS-06
  // ═════════════════════════════════════════════════════════════════════
  const double L = 6.0, B = 1.5, D = 1.0, T = 0.3, KG = 0.5, rho = 1000.0;

  std::printf("--- HS-06：L=%.1f B=%.1f D=%.1f T=%.2f KG=%.2f ---\n", L, B, D, T, KG);

  keship::GzInput in;
  in.L = L; in.B = B; in.D = D; in.draft = T; in.KG = KG; in.rhoWater = rho;
  in.boxSection = true;

  const keship::hs06::WallSidedModel model =
      keship::hs06::BuildWallSidedModel({}, true, B, L, T, KG);
  Check(model.valid, "wall-sided 解析模型构造成功（矩形剖面 b = B/2）");
  std::printf("      KB=%.6f  BM=%.6f  GM=%.6f  ∇=%.6f  Ix=%.6f\n",
              model.kbM, model.bmM, model.gmM, model.volumeM3, model.ixM4);

  const keship::hs06::SampleTable tab =
      keship::hs06::SampleWallSided(in, model, 60.0, 0.5, 5.0);
  Check(!tab.rows.empty() && std::abs(tab.rows.front().phiDeg) < 1e-12,
        "采样表非空且从 φ=0 起算");

  // ① φ=0
  {
    const double gN = keship::GZ(0.0, in);
    const double gW = keship::hs06::WallSidedGz(0.0, model);
    const double d = std::abs(gN - gW);
    std::printf("      φ=0: GZ_numeric=%.17g  GZ_wallSided=%.17g  |Δ|=%.3e\n", gN, gW, d);
    Check(d < 1e-9, "① φ=0 两法差 < 1e-9");
  }

  // ② 小角偏差 + 单调性
  {
    const double d5 = DevAt(tab, 5.0);
    const double d10 = DevAt(tab, 10.0);
    const double d20 = DevAt(tab, 20.0);
    const double d30 = DevAt(tab, 30.0);
    std::printf("      偏差：5°=%.6f%%  10°=%.6f%%  20°=%.6f%%  30°=%.6f%%\n",
                d5, d10, d20, d30);
    Check(d5 >= 0.0 && d5 < 1.0, "② 5° 处两法相对偏差 < 1%");
    // 5/10/20° 两法在理论上严格等价，偏差只剩浮点噪声（~1e-13 个百分点）；
    // 单调性按 1e-6 个百分点容差判定，不把舍入噪声当"反弹"。
    const double eps = 1e-6;
    Check(d10 >= d5 - eps && d20 >= d10 - eps && d30 >= d20 - eps,
          "② 偏差随 φ 单调不减（5→10→20→30°）");
    Check(d30 > d20, "② 30° 偏差显著大于 20°（舭部出水后公式失准）");
  }

  // ③ 特征角
  {
    const double bilge = Rad2Deg(std::atan(2.0 * T / B));
    const double deck = Rad2Deg(std::atan(2.0 * (D - T) / B));
    const keship::hs06::FeatureAngles f = keship::hs06::ComputeFeatureAngles(B, T, D);
    std::printf("      舭部出水=%.9f°（手算 %.9f°）  甲板入水=%.9f°（手算 %.9f°）\n",
                f.bilgeDeg, bilge, f.deckDeg, deck);
    Check(std::abs(f.bilgeDeg - bilge) < 1e-6, "③ 舭部出水角 = atan(2T/B)");
    Check(std::abs(f.deckDeg - deck) < 1e-6, "③ 甲板入水角 = atan(2(D−T)/B)");
    std::printf("      wall-sided 失效点（偏差首次 >5%%）= %.1f°（最大偏差 %.2f%%）\n",
                tab.validUntilDeg, tab.maxDevPct);
    Check(tab.validUntilDeg > f.bilgeDeg && tab.validUntilDeg < 40.0,
          "③ 失效点落在「舭部出水」之后（物理前提先破，数值偏差后到 5%）");
    Check(tab.validUntilDeg == 26.0, "③ 基准船型失效点 = 26.0°（0.5° 扫描首次 >5%）");
  }

  // ③b 「非矩形剖面」分支：换数学船型后两法仍应小角重合、大角分离
  {
    LargeAngleStabilityDialog dlg;
    const bool ok = dlg.setBoxSection(false);
    Check(ok, "③b 切到数学船型剖面（boxSection = false）后重算成功" +
                  (ok ? std::string() : "：" + dlg.errMsg().toStdString()));
    std::printf("      数学船型：GM=%.6f m  GZmax=%.6f m  失效点=%.1f°  最大偏差=%.2f%%\n",
                dlg.gm0M(), dlg.gzMaxM(), dlg.wallSidedValidUntilDeg(), dlg.maxDeviationPct());
    Check(dlg.gzMaxM() > 0.0, "③b 数学船型的 GZmax > 0");
    Check(std::abs(dlg.bilgeEmergenceDeg() - Rad2Deg(std::atan(2.0 * T / B))) < 1e-6,
          "③b 特征角仍按最大宽度站（b = B/2）给出");
    Check(dlg.wallSidedValidUntilDeg() > dlg.bilgeEmergenceDeg() &&
              dlg.wallSidedValidUntilDeg() < 45.0,
          "③b 数学船型的失效点同样落在「舭部出水」之后");

    // 换船型后"解析 GM"与"数值曲线 0° 斜率"仍须一致（同一套站表口径的判据）
    const QString tmp = "/tmp/hs06_gz_mathhull_test.json";
    if (dlg.exportTo(tmp)) {
      keship::ExpDeliverable dm;
      std::string e2;
      if (keship::ReadExpDeliverable(tmp.toStdString(), dm, &e2)) {
        const double a = dm.metrics["gm0M"].toDouble();
        const double b = dm.extra["wallSided"].toObject()["gm0NumericM"].toDouble();
        std::printf("      数学船型对账：gm0M(解析)=%.6f  gm0NumericM=%.6f  相对差=%.3f%%\n", a, b,
                    100.0 * std::abs(a - b) / std::max(std::abs(a), 1e-9));
        Check(std::abs(a - b) <= 0.02 * std::max(std::abs(a), 1e-9),
              "③b 数学船型下解析 GM 与数值斜率一致（相对差 ≤ 2%）");
      }
    }
    dlg.setBoxSection(true);
  }

  // ④ 交付物往返
  {
    const QString path = "/tmp/hs06_gz_test.json";
    LargeAngleStabilityDialog dlg;
    const bool ok = dlg.exportTo(path);
    Check(ok, "④ HS-06 对话框离屏导出交付物" +
                  (ok ? std::string() : "：" + dlg.errMsg().toStdString()));

    keship::ExpDeliverable d;
    std::string err;
    const bool readOk = keship::ReadExpDeliverable(path.toStdString(), d, &err);
    Check(readOk, "④ 读回交付物" + (err.empty() ? std::string() : "：" + err));

    if (readOk) {
      err.clear();
      Check(keship::ValidExpDeliverable(d, "HS-06", &err),
            "④ Valid 接受 HS-06 交付物（expId=" + d.expId + "）");

      err.clear();
      Check(!keship::ValidExpDeliverable(d, "SD-02", &err),
            "④ Valid 拒绝把 HS-06 交付物当 SD-02 用（负例）");

      const double gm0 = d.metrics["gm0M"].toDouble();
      const double bilge = d.metrics["bilgeEmergenceDeg"].toDouble();
      const double deck = d.metrics["deckImmersionDeg"].toDouble();
      const double validUntil = d.metrics["wallSidedValidUntilDeg"].toDouble();
      std::printf("      往返：gm0M=%.9f  bilge=%.9f  deck=%.9f  validUntil=%.9f  "
                  "maxDev=%.6f  table=%d 点\n",
                  gm0, bilge, deck, validUntil, d.metrics["maxDeviationPct"].toDouble(),
                  static_cast<int>(d.extra["table"].toArray().size()));

      Check(std::abs(gm0 - dlg.gm0M()) < 1e-12, "④ gm0M 往返逐位一致");
      Check(std::abs(gm0 - model.gmM) < 1e-12, "④ gm0M 与解析 GM = KB+BM−KG 一致");
      const double gmNum = d.extra["wallSided"].toObject()["gm0NumericM"].toDouble();
      std::printf("      gm0M(解析)=%.9f  gm0NumericM(数值斜率)=%.9f  |Δ|=%.2e\n",
                  gm0, gmNum, std::abs(gmNum - gm0));
      Check(std::abs(gmNum - gm0) < 1e-5,
            "④ 数值曲线在 0° 的斜率 = 解析 GM（差 < 1e-5，仅中心差分误差）");
      Check(std::abs(bilge - dlg.bilgeEmergenceDeg()) < 1e-12, "④ bilgeEmergenceDeg 往返一致");
      Check(std::abs(deck - dlg.deckImmersionDeg()) < 1e-12, "④ deckImmersionDeg 往返一致");
      Check(std::abs(validUntil - dlg.wallSidedValidUntilDeg()) < 1e-12,
            "④ wallSidedValidUntilDeg 往返一致");
      Check(std::abs(d.metrics["maxDeviationPct"].toDouble() - dlg.maxDeviationPct()) < 1e-12,
            "④ maxDeviationPct 往返一致");
      Check(d.metrics.contains("gzMaxM") && d.metrics.contains("angleGzMaxDeg") &&
                d.metrics.contains("boxSection"),
            "④ metrics 含 gzMaxM / angleGzMaxDeg / boxSection");
      Check(d.params.contains("assumptions") &&
                !d.params["assumptions"].toString().isEmpty(),
            "④ params.assumptions 已写明（封闭箱体 / 舷侧直立 / 甲板入水后外推）");

      const QJsonArray tbl = d.extra["table"].toArray();
      Check(tbl.size() == 121, "④ extra.table = 0…60° 每 0.5° 共 121 行");
      if (tbl.size() == 121) {
        const QJsonObject r0 = tbl[0].toObject();
        const QJsonObject rN = tbl[120].toObject();
        Check(std::abs(r0["phi"].toDouble()) < 1e-12 &&
                  std::abs(r0["gzNumeric"].toDouble()) < 1e-12 &&
                  std::abs(r0["gzWallSided"].toDouble()) < 1e-12,
              "④ 表首行 φ=0、两法 GZ 均为 0");
        Check(std::abs(rN["phi"].toDouble() - 60.0) < 1e-12, "④ 表末行 φ=60°");
        Check(r0["valid"].toBool() && !rN["valid"].toBool(),
              "④ valid 标志：0° 有效、60° 失效");
      }
    }
  }

  // ═════════════════════════════════════════════════════════════════════
  // SD-02
  // ═════════════════════════════════════════════════════════════════════
  std::printf("--- SD-02：Wigley 经典 L=%.1f B=%.1f D=%.1f ---\n", L, B, D);

  keship::ShipHullGenerator hull(keship::ShipHullSpec::WigleyClassic(L, B, D, 500.0));
  const keship::LinesData &ld = hull.lines();
  const int nSt = static_cast<int>(ld.stations.size());
  const int nWl = static_cast<int>(ld.waterlines.size());
  std::printf("      型值表：%d 站 × %d 水线；z ∈ [%.3f, %.3f]\n", nSt, nWl,
              ld.waterlines.empty() ? 0.0 : ld.waterlines.back(),
              ld.waterlines.empty() ? 0.0 : ld.waterlines.front());
  Check(nSt >= 11 && nWl >= 6, "SD-02 型值表分辨率足够（≥11 站 × ≥6 水线）");

  const std::vector<keship::sd02::Question> qs =
      keship::sd02::GenerateQuestions(ld, 5, 20261028u);
  Check(static_cast<int>(qs.size()) == 5, "⑤ 抽到 5 道题");
  if (static_cast<int>(qs.size()) == 5) {
    int nHalf = 0, nWl2 = 0, nIn = 0;
    for (const auto &q : qs) {
      if (q.type == keship::sd02::QType::kHalfBreadth) ++nHalf;
      if (q.type == keship::sd02::QType::kWaterline) ++nWl2;
      if (q.type == keship::sd02::QType::kInside) ++nIn;
    }
    std::printf("      题型分布：求半宽 %d / 求水线 %d / 判断题 %d\n", nHalf, nWl2, nIn);
    Check(nHalf >= 1 && nWl2 >= 1 && nIn >= 1, "⑤ 三种题型各至少一题");
  }

  // ⑤ 标准答案 == 直接查表（逐位）
  {
    bool allSame = true;
    for (std::size_t k = 0; k < qs.size(); ++k) {
      keship::sd02::Question q = qs[k];  // JudgeQuestion 会重算标准答案，用副本
      switch (q.type) {
        case keship::sd02::QType::kHalfBreadth: {
          const double direct = ld.halfBreadths[static_cast<std::size_t>(q.stationIdx)]
                                                 [static_cast<std::size_t>(q.waterlineIdx)];
          if (direct != q.correctNumber) allSame = false;
          keship::sd02::JudgeQuestion(q, ld, 0.5);
          if (q.correctNumber != direct) allSame = false;  // 判定时重算也必须同值
          std::printf("      #%d 求半宽：站位 %d × 水线 %d → %.6f m（查表 %.6f）\n",
                      static_cast<int>(k) + 1, q.stationIdx, q.waterlineIdx,
                      q.correctNumber, direct);
          break;
        }
        case keship::sd02::QType::kWaterline: {
          // 标准答案 = 该站上半宽最接近 targetHalfBreadth 的水线序号
          const auto &row = ld.halfBreadths[static_cast<std::size_t>(q.stationIdx)];
          int bestJ = -1;
          double bestD = 1e18;
          for (int j = 0; j < static_cast<int>(row.size()); ++j) {
            const double d = std::abs(row[static_cast<std::size_t>(j)] - q.targetHalfBreadth);
            if (d < bestD) { bestD = d; bestJ = j; }
          }
          if (static_cast<double>(bestJ) != q.correctNumber) allSame = false;
          if (row[static_cast<std::size_t>(bestJ)] != q.targetHalfBreadth) allSame = false;
          keship::sd02::JudgeQuestion(q, ld, 0.5);
          if (q.correctNumber != static_cast<double>(bestJ)) allSame = false;
          std::printf("      #%d 求水线：站位 %d 上 y=%.6f m → 水线 #%d（查表 #%d）\n",
                      static_cast<int>(k) + 1, q.stationIdx, q.targetHalfBreadth, bestJ, bestJ);
          break;
        }
        case keship::sd02::QType::kInside: {
          const double b = keship::sd02::HalfBreadthAt(ld, q.probeX, q.probeZ);
          const bool direct = std::abs(q.probeY) <= b;
          if (direct != q.correctBool) allSame = false;
          keship::sd02::JudgeQuestion(q, ld, 0.5);
          if (q.correctBool != direct) allSame = false;
          std::printf("      #%d 判断内外：点(%.3f, %.4f, %.3f) 半宽=%.6f → %s\n",
                      static_cast<int>(k) + 1, q.probeX, q.probeY, q.probeZ, b,
                      direct ? "在船体内" : "在船体外");
          break;
        }
      }
    }
    Check(allSame, "⑤ 5 道题的标准答案与直接查表逐位一致（含判定时重算）");
  }

  // ⑥ 对错判定
  {
    keship::sd02::Question qn;
    qn.type = keship::sd02::QType::kHalfBreadth;
    qn.stationIdx = nSt / 2;
    qn.waterlineIdx = nWl / 2;
    const double truth = ld.halfBreadths[static_cast<std::size_t>(qn.stationIdx)]
                                       [static_cast<std::size_t>(qn.waterlineIdx)];

    qn.studentNumber = truth;
    keship::sd02::JudgeQuestion(qn, ld, 0.5);
    const bool rightHalf = qn.passed;

    qn.studentNumber = truth * 1.2;  // 偏差 20%，远超 ±0.5% 容差
    keship::sd02::JudgeQuestion(qn, ld, 0.5);
    const bool wrongHalf = qn.passed;

    // 容差边界：+0.4% 应判对、+0.6% 应判错（±0.5% 口径不是摆设）
    qn.studentNumber = truth * 1.004;
    keship::sd02::JudgeQuestion(qn, ld, 0.5);
    const bool insideTol = qn.passed;
    qn.studentNumber = truth * 1.006;
    keship::sd02::JudgeQuestion(qn, ld, 0.5);
    const bool outsideTol = qn.passed;

    std::printf("      半宽题 truth=%.6f：答对=%d 答错(×1.2)=%d  +0.4%%=%d  +0.6%%=%d\n",
                truth, rightHalf ? 1 : 0, wrongHalf ? 1 : 0, insideTol ? 1 : 0,
                outsideTol ? 1 : 0);
    Check(rightHalf, "⑥ 半宽题答对 → passed = true");
    Check(!wrongHalf, "⑥ 半宽题答错 → passed = false");
    Check(insideTol && !outsideTol, "⑥ 半宽题容差边界：+0.4% 判对、+0.6% 判错");

    // 判断题：正反各一例（取一条必在船内、一条必在船外的点）
    keship::sd02::Question qi;
    qi.type = keship::sd02::QType::kInside;
    qi.probeX = ld.stations[static_cast<std::size_t>(nSt / 2)];
    qi.probeZ = 0.5 * (ld.waterlines.front() + ld.waterlines.back());
    const double bHere = keship::sd02::HalfBreadthAt(ld, qi.probeX, qi.probeZ);
    Check(bHere > 0.0, "⑥ 判断点的半宽可用（插值非零）");

    qi.probeY = 0.5 * bHere;
    qi.studentBool = true;
    keship::sd02::JudgeQuestion(qi, ld, 0.5);
    const bool inRight = qi.passed;

    qi.probeY = 2.0 * bHere;
    qi.studentBool = false;
    keship::sd02::JudgeQuestion(qi, ld, 0.5);
    const bool outRight = qi.passed;

    qi.studentBool = true;  // 故意答错：把船外的点判成"在船内"
    keship::sd02::JudgeQuestion(qi, ld, 0.5);
    const bool outWrong = qi.passed;

    std::printf("      判断题：半宽=%.6f，y=0.5b 答是→%d，y=2b 答否→%d，y=2b 答是→%d\n",
                bHere, inRight ? 1 : 0, outRight ? 1 : 0, outWrong ? 1 : 0);
    Check(inRight, "⑥ 判断题答对（船内点答「是」）→ passed = true");
    Check(outRight, "⑥ 判断题答对（船外点答「否」）→ passed = true");
    Check(!outWrong, "⑥ 判断题答错（船外点答「是」）→ passed = false");
  }

  // ⑦ 分数 == passed 计数
  {
    std::vector<keship::sd02::Question> v = qs;
    int manual = 0;
    for (std::size_t k = 0; k < v.size(); ++k) {
      keship::sd02::Question &q = v[k];
      // 交替：偶数题答对、奇数题答错，构造"部分得分"的样本
      const bool wantRight = (k % 2 == 0);
      if (q.type == keship::sd02::QType::kHalfBreadth) {
        keship::sd02::JudgeQuestion(q, ld, 0.5);
        q.studentNumber = wantRight ? q.correctNumber : q.correctNumber * 1.5;
      } else if (q.type == keship::sd02::QType::kWaterline) {
        keship::sd02::JudgeQuestion(q, ld, 0.5);
        q.studentNumber = wantRight ? q.correctNumber : q.correctNumber + 3.0;
      } else {
        keship::sd02::JudgeQuestion(q, ld, 0.5);
        q.studentBool = wantRight ? q.correctBool : !q.correctBool;
      }
      keship::sd02::JudgeQuestion(q, ld, 0.5);
      if (q.passed) ++manual;
    }
    const int sc = keship::sd02::ScoreOf(v);
    std::printf("      ScoreOf=%d，逐题 passed 计数=%d（共 %d 题）\n", sc, manual,
                static_cast<int>(v.size()));
    Check(sc == manual, "⑦ score 与逐题 passed 计数一致");
    Check(sc == 3, "⑦ 交替作答下得分为 3/5（偶数题对、奇数题错）");
  }

  // ⑦b SD-02 交付物（非链，但同样必须机器可读）
  {
    LinesReadingDialog dlg;
    const QString path = "/tmp/sd02_lines_reading_test.json";
    const bool ok = dlg.exportTo(path);
    Check(ok, "⑦ SD-02 对话框离屏导出交付物" +
                  (ok ? std::string() : "：" + dlg.errMsg().toStdString()));

    keship::ExpDeliverable d;
    std::string err;
    const bool readOk = keship::ReadExpDeliverable(path.toStdString(), d, &err);
    Check(readOk, "⑦ 读回 SD-02 交付物" + (err.empty() ? std::string() : "：" + err));
    if (readOk) {
      err.clear();
      Check(keship::ValidExpDeliverable(d, "SD-02", &err), "⑦ Valid 接受 SD-02 交付物");
      err.clear();
      Check(!keship::ValidExpDeliverable(d, "HS-06", &err),
            "⑦ Valid 拒绝把 SD-02 交付物当 HS-06 用（负例）");

      const QJsonArray arr = d.extra["questions"].toArray();
      Check(arr.size() == 5, "⑦ extra.questions 共 5 题");
      Check(d.metrics.contains("score") && d.metrics.contains("totalQuestions") &&
                d.metrics.contains("accuracy") && d.metrics.contains("nStations") &&
                d.metrics.contains("nWaterlines") &&
                d.metrics.contains("maxHalfBreadthM"),
            "⑦ metrics 含 score / totalQuestions / accuracy / nStations / nWaterlines / "
            "maxHalfBreadthM");
      Check(std::abs(d.metrics["maxHalfBreadthM"].toDouble() - B / 2.0) < 1e-9,
            "⑦ maxHalfBreadthM = B/2（Wigley 水线面最宽处）");
      if (arr.size() == 5) {
        const QJsonObject q0 = arr[0].toObject();
        Check(q0.contains("type") && q0.contains("stationIdx") && q0.contains("waterlineIdx") &&
                  q0.contains("studentAnswer") && q0.contains("correctAnswer") &&
                  q0.contains("passed"),
              "⑦ 每题含 type/stationIdx/waterlineIdx/studentAnswer/correctAnswer/passed");
        Check(d.metrics["totalQuestions"].toInt() == 5 &&
                  d.metrics["score"].toInt() == dlg.score(),
              "⑦ 交付物分数与对话框当前得分一致");
      }
    }
  }

  std::printf("--- 汇总：%s（失败 %d 项）---\n", gFail == 0 ? "全部 PASS" : "存在 FAIL", gFail);
  return gFail == 0 ? 0 : 1;
}
