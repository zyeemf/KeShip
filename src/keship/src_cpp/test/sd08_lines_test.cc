/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-08 参数化型线设计 —— T1 离屏测试
 *
 * 覆盖（判据 B 的机器交付物环节）：
 *   ① Cb 口径自洽：Cb == volumeD/(L·B·T)，且 ∈ (0,1)
 *   ② 静水力恒等式：BMd == IxD/volumeD、GMd == KBup + BMd − D/2
 *   ③ Cb 对水线面指数 p 单调递增（「按目标 Cb 反解」的合法性前提；
 *      若不单调，反解会给出看似合理其实错误的 p —— 故必须由测试守住）
 *   ④ 型值表自洽：尺寸对齐、艏艉半宽趋零、中站半宽最大且 ≤ B/2
 *   ⑤ 交付物写→读→校验往返：metrics 逐位一致；expId 不符时 Valid 必须拒绝（负例）
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <QJsonObject>

#include "../gui/keship/shared/ExpDeliverable.hh"
#include "ShipHullGenerator.hh"

namespace {

int gFail = 0;

void Check(bool _cond, const std::string &_msg)
{
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

bool Near(double _a, double _b, double _rel = 1e-9)
{
  const double d = std::abs(_a - _b);
  const double scale = std::max({1.0, std::abs(_a), std::abs(_b)});
  return d <= _rel * scale;
}

double CbOf(const keship::ShipHullSpec &_base, double _p)
{
  keship::ShipHullSpec s = _base;
  s.wlExponent = _p;
  keship::ShipHullGenerator g(s);
  const auto &h = g.hydroProps();
  return (h.draft > 1e-9) ? h.volumeD / (s.L * s.B * h.draft) : 0.0;
}

}  // namespace

int main()
{
  using keship::ShipHullGenerator;
  using keship::ShipHullSpec;

  // ── 基准船型：Wigley 经典，L=6 / B=1.5 / D=1.0 / ρ=500 ──
  const ShipHullSpec base = ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0, 500.0);
  ShipHullGenerator hull(base);
  const auto &hp = hull.hydroProps();
  const auto &ld = hull.lines();

  std::printf("--- SD-08 T1：基准船型 L=%.1f B=%.1f D=%.1f ρ=%.0f ---\n",
              base.L, base.B, base.D, base.rho);
  std::printf("volumeD=%.6f draft=%.6f Cb=%.6f Aw=%.6f Cwp=%.6f KB=%.6f BM=%.6f GM=%.6f\n",
              hp.volumeD, hp.draft, hp.volumeD / (base.L * base.B * hp.draft),
              hp.AwD, hp.Cwp, hp.KBup, hp.BMd, hp.GMd);

  // ① Cb 口径
  const double cb = hp.volumeD / (base.L * base.B * hp.draft);
  Check(hp.draft > 0.0 && hp.draft < base.D,
        "设计吃水 T 落在 (0, D) 内");
  Check(cb > 0.2 && cb < 0.95, "Cb 落在合理区间 (0.2, 0.95)");
  Check(Near(hp.volumeD, base.L * base.B * hp.draft * cb, 1e-12),
        "Cb 口径自洽：volumeD == L·B·T·Cb");

  // ② 静水力恒等式
  Check(Near(hp.BMd, hp.IxD / hp.volumeD, 1e-9),
        "恒等式 BMd == IxD / volumeD");
  Check(Near(hp.GMd, hp.KBup + hp.BMd - base.D / 2.0, 1e-9),
        "恒等式 GMd == KBup + BMd − D/2");
  Check(hp.Cwp > 0.3 && hp.Cwp <= 1.0, "Cwp ∈ (0.3, 1.0]");
  Check(hp.GMd > 0.0, "基准船型 GM > 0（正稳性）");

  // ③ Cb 对 p 单调（反解前提）
  bool mono = true;
  double prev = CbOf(base, 1.0);
  std::printf("Cb(p) 采样：");
  for (double p : {1.0, 1.5, 2.0, 3.0, 4.0, 6.0}) {
    const double v = CbOf(base, p);
    std::printf(" p=%.1f→%.4f", p, v);
    if (v <= prev && p > 1.0) mono = false;
    prev = v;
  }
  std::printf("\n");
  Check(mono, "Cb 随水线面指数 p 单调递增（反解合法）");

  // ④ 型值表自洽
  const size_t nSt = ld.stations.size();
  const size_t nWl = ld.waterlines.size();
  Check(nSt >= 11 && nWl >= 6, "型值表分辨率足够（≥11 站 × ≥6 水线）");
  Check(ld.halfBreadths.size() == nSt, "halfBreadths 行数 == 站位数");

  bool shapeOk = true;
  for (size_t i = 0; i < nSt; ++i)
    if (ld.halfBreadths[i].size() != nWl) shapeOk = false;
  Check(shapeOk, "每站半宽点数 == 水线数");

  const double bowHalf = ld.halfBreadths.front().back();
  const double sternHalf = ld.halfBreadths.back().back();
  double maxHalf = 0.0;
  for (const auto &row : ld.halfBreadths)
    for (double v : row) maxHalf = std::max(maxHalf, v);
  Check(bowHalf < 0.02 * maxHalf, "艏端半宽趋零（型线收窄）");
  Check(sternHalf < 0.02 * maxHalf, "艉端半宽趋零（型线收窄）");
  Check(maxHalf <= base.B / 2.0 + 1e-9, "最大半宽 ≤ B/2");

  // ⑤ 交付物往返
  keship::ExpDeliverable d;
  d.expId = "SD-08";
  d.createdAt = keship::NowIso8601();
  d.object["type"] = "math_hull";
  d.object["lengthM"] = base.L;
  d.object["breadthM"] = base.B;
  d.object["depthM"] = base.D;
  d.params["wlExponent"] = base.wlExponent;
  d.condition["draftM"] = hp.draft;
  d.metrics["volumeD"] = hp.volumeD;
  d.metrics["cb"] = cb;
  d.metrics["GMd"] = hp.GMd;
  d.metrics["IxD"] = hp.IxD;
  d.metrics["BMd"] = hp.BMd;
  d.metrics["KBup"] = hp.KBup;
  d.extra["nStations"] = static_cast<int>(nSt);

  const std::string path = "/tmp/sd08_t1_deliverable.json";
  std::string err;
  Check(keship::WriteExpDeliverable(path, d, &err),
        "写交付物成功（/tmp/sd08_t1_deliverable.json）" +
            (err.empty() ? std::string() : "：" + err));

  keship::ExpDeliverable back;
  err.clear();
  const bool readOk = keship::ReadExpDeliverable(path, back, &err);
  Check(readOk, "读交付物成功" + (err.empty() ? std::string() : "：" + err));

  if (readOk) {
    Check(back.expId == "SD-08", "往返后 expId 保持 SD-08");
    Check(Near(back.metrics["volumeD"].toDouble(), hp.volumeD, 1e-12),
          "往返后 volumeD 逐位一致");
    Check(Near(back.metrics["GMd"].toDouble(), hp.GMd, 1e-12),
          "往返后 GMd 逐位一致");
    Check(Near(back.metrics["cb"].toDouble(), cb, 1e-12),
          "往返后 cb 逐位一致");

    err.clear();
    Check(keship::ValidExpDeliverable(back, "SD-08", &err),
          "Valid 接受 SD-08 交付物");

    // 负例：串号必须被拒（防止下游静默用错实验的数据）
    err.clear();
    Check(!keship::ValidExpDeliverable(back, "HS-05", &err),
          "Valid 拒绝把 SD-08 交付物当 HS-05 用（负例）");
  }

  std::printf("--- SD-08 T1 结果：%s（失败 %d 项）---\n",
              gFail == 0 ? "PASS" : "FAIL", gFail);
  return gFail == 0 ? 0 : 1;
}
