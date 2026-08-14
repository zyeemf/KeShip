
# 2026.08.11 

## 先梳理你的核心矛盾（Fossen船舶模型 + Gazebo自研求解）
标准Fossen船舶动力学：
$$
\boldsymbol{M}\dot{\boldsymbol{\nu}} = \boldsymbol{C}(\boldsymbol{\nu})\boldsymbol{\nu} + \boldsymbol{D}(\boldsymbol{\nu})\boldsymbol{\nu} + \boldsymbol{g}(\boldsymbol{\eta}) + \boldsymbol{\tau}
$$
总质量矩阵
$$
\boldsymbol{M} = \boldsymbol{M}_{RB} + \boldsymbol{M}_A
$$
- $\boldsymbol{M}_{RB}$：刚体质量惯性矩阵（常数，隐式，和加速度耦合）
- $\boldsymbol{M}_A$：附加质量（你希望**显式、运行时动态修改**）

原式展开：
$$
(\boldsymbol{M}_{RB}+\boldsymbol{M}_A)\dot{\boldsymbol{\nu}} = \boldsymbol{(\boldsymbol{h})}(\boldsymbol{\eta},\boldsymbol{\nu}) + \boldsymbol{\tau}
$$
其中 $\boldsymbol{(\boldsymbol{h})}$ 包含科氏力、阻尼、静恢复力。
整理得到加速度形式：
$$
\dot{\boldsymbol{\nu}} = (\boldsymbol{M}_{RB}+\boldsymbol{M}_A)^{-1}\big(\boldsymbol{(\boldsymbol{h})}+\boldsymbol{\tau}\big)
$$

### 你的目标拆分思路
把 $\boldsymbol{M}_A$ 剥离出来，做到：
1. $\boldsymbol{M}_{RB}$ 保留在左侧（刚体固有特性不变）
2. $\boldsymbol{M}_A$ 移到右侧，**显式，支持在线动态更新**

代数变形：
$$
\boldsymbol{M}_{RB}\dot{\boldsymbol{\nu}} = \boldsymbol{(\boldsymbol{h})}+\boldsymbol{\tau} - \boldsymbol{M}_A\dot{\boldsymbol{\nu}}
$$
👉 关键问题：右侧出现 $\boldsymbol{M}_A\dot{\boldsymbol{\nu}}$，$\dot{\boldsymbol{\nu}}$ 是待求未知量，**形成代数环**。
这就是你当前遇到代数环的来源。

---

# 四种工程可行方案（适配Gazebo船舶仿真，按推荐优先级排序）
## 方案1：定点迭代法【首选，不改变模型物理含义，精度最高】
每一个仿真步内迭代求解消除代数环，适合Gazebo离散步进求解框架。
迭代格式：
1. 迭代初值：用上一步加速度 $\dot{\boldsymbol{\nu}}^{[0]} = \dot{\boldsymbol{\nu}}_{k-1}$
2. 更新右端附加质量项：$\boldsymbol{f}_A^{[i]} = \boldsymbol{M}_A \dot{\boldsymbol{\nu}}^{[i]}$
3. 求解刚体加速度：
$$
\boldsymbol{M}_{RB}\dot{\boldsymbol{\nu}}^{[i+1]} = \boldsymbol{(\boldsymbol{h})}_k + \boldsymbol{\tau}_k - \boldsymbol{f}_A^{[i]}
$$
4. 收敛判定：$\|\dot{\boldsymbol{\nu}}^{[i+1]}-\dot{\boldsymbol{\nu}}^{[i]}\|<\epsilon$，达到最大迭代次数终止

✅优点
- 严格保持Fossen原始方程；$\boldsymbol{M}_A$ 随时动态替换；
- 无引入人为动态滞后；适合船舶运动高精度仿真。
⚠️代价：每步若干次矩阵求解；需调试收敛阈值。

> Gazebo插件（C++）非常适合实现，船舶仿真领域大量文献采用该方式处理分离形式附加质量。

## 方案2：一阶正则化（惯性软化，次选，代码最简单）
引入极小时间常数 $\tau$，构造辅助状态隔离代数耦合：
$$
\tau \dot{\boldsymbol{f}}_A = \boldsymbol{M}_A\dot{\boldsymbol{\nu}} - \boldsymbol{f}_A
$$
系统变为：
$$
\boldsymbol{M}_{RB}\dot{\boldsymbol{\nu}} = \boldsymbol{(\boldsymbol{h})}+\boldsymbol{\tau}-\boldsymbol{f}_A
$$
$\boldsymbol{f}_A$ 作为独立状态参与积分，代数环彻底消失。
$\tau$ 取很小值，如 $10^{-4}\sim10^{-6}$。

✅优点：不需要迭代；极易编码；$\boldsymbol{M}_A$ 实时修改直接生效
⚠️缺点：引入极微小动态；$\tau$ 过小易造成刚性问题，积分步长不能太大。

## 方案3：一步延迟近似（实时仿真算力受限应急方案）
使用上一步加速度近似当前附加质量力：
$$
\boldsymbol{M}_{RB}\dot{\boldsymbol{\nu}}_k = \boldsymbol{(\boldsymbol{h})}_k+\boldsymbol{\tau}_k - \boldsymbol{M}_{A,k}\dot{\boldsymbol{\nu}}_{k-1}
$$
✅实现最简单，无迭代、无额外状态
❌缺点：存在相位误差；高速机动、大幅机动工况误差明显；只推荐快速原型调试。

## 方案4：DAE隐式积分（开发成本最高，追求极致精度可选）
不拆分方程，直接把系统看作微分代数方程组，使用隐式BDF/Radau求解器整体求解。
不推荐Gazebo场景：Gazebo自带ODE物理引擎是显式步进，深度改造求解框架工作量巨大。

---

# 推荐落地路线（针对你的需求）
> 目标：$\boldsymbol{M}_{RB}$ 隐式，$\boldsymbol{M}_A$ 显式可动态修改
**优先采用【定点迭代方案】**
标准执行流程嵌入Gazebo每个Update步：
1. 获取当前时刻姿态 $\boldsymbol{\eta}$、速度 $\boldsymbol{\nu}$，计算 $\boldsymbol{(\boldsymbol{h})}(\boldsymbol{\eta},\boldsymbol{\nu})$
2. 获取当前控制力矩 $\boldsymbol{\tau}$，载入最新更新的 $\boldsymbol{M}_A$
3. 启动定点迭代求解 $\dot{\boldsymbol{\nu}}$
4. 得到收敛的加速度，送入积分更新船舶速度与位置

### 补充实操提示
1. $\boldsymbol{M}_{RB}$ 正定对称，求逆/线性求解稳定；
2. 迭代初值用上一步加速度，一般2～5轮即可收敛；
3. 如果后续要做变附加质量（变吃水、航速相关 $M_A(\nu)$），这套框架完全兼容；
4. 对比：如果你直接合并 $\boldsymbol{M}=\boldsymbol{M}_{RB}+\boldsymbol{M}_A$ 再求逆，每次修改 $M_A$ 都要重新组装矩阵求逆，和本方案计算量接近，但无法实现你想要的“RB/A物理层面分离”架构。

## 论文可用英文表述
T\(\boldsymbol{h}\)e rigid-body mass matrix $\boldsymbol{M}_{RB}$ is kept in implicit form, w\(\boldsymbol{h}\)ile t\(\boldsymbol{h}\)e added mass matrix $\boldsymbol{M}_A$ is treated explicitly to support runtime dynamic modification. T\(\boldsymbol{h}\)e resulting algebraic loop is eliminated via fixed-point iteration wit\(\boldsymbol{h}\)in eac\(\boldsymbol{h}\) time step.



