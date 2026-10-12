#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
S1 补丁：给 gz-sim 加"运行时设置流体附加质量"的通路。

  ★ 关键：S1 阶段**没有任何调用方** ⇒ 组件永不出现 ⇒ 行为零变更。
  验收判据：落体实验 a_on/a_off 仍为 0.5263（逐位不变）。

改动 4 处：
  1. include/gz/sim/Link.hh      —— 加 SetFluidAddedMass 声明
  2. src/Link.cc                 —— include 新组件 + 加实现
  3. src/systems/physics/Physics.cc —— include + FeatureList + EntityLinkMap + 消费

精确匹配替换（不唯一即整体放弃）+ 备份 .bak_s1
"""
import hashlib
import shutil
import sys
from pathlib import Path

SRC = Path("/home/keship/gz-build/gz-sim-8.11.0")


def md5(p):
    return hashlib.md5(Path(p).read_bytes()).hexdigest()


def apply(path, pairs, label):
    p = Path(path)
    if not p.is_file():
        print("  [FAIL] %s: 文件不存在 %s" % (label, p))
        return False
    s = p.read_text(encoding="utf-8")
    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            print("  [FAIL] %s: 匹配数=%d（应为 1），片段=%r" % (label, n, old[:80]))
            return False
        s = s.replace(old, new)
    shutil.copy2(str(p), str(p) + ".bak_s1")
    p.write_text(s, encoding="utf-8")
    return True


# ---------------------------------------------------------------- 1) Link.hh
LINK_HH_OLD = """      public: void AddWorldWrench(EntityComponentManager &_ecm,
                                  const math::Vector3d &_force,
                                  const math::Vector3d &_torque,
                                  const math::Vector3d &_offset) const;

      /// \\brief By default, Gazebo will not report bounding box for a link, so"""

LINK_HH_NEW = """      public: void AddWorldWrench(EntityComponentManager &_ecm,
                                  const math::Vector3d &_force,
                                  const math::Vector3d &_torque,
                                  const math::Vector3d &_offset) const;

      /// \\brief Set the fluid added mass of this link **at runtime**.
      ///
      /// By default the added mass is only injected once, from the SDF
      /// `<fluid_added_mass>` element, when the entity is created; it can never
      /// be changed afterwards. This method writes an
      /// `ExternalFluidAddedMassCmd` component which the Physics system
      /// forwards to `gz::physics::AddedMass::Link::SetAddedMass()`, so the
      /// value takes effect on the next physics step.
      ///
      /// \\note Added by KeShip (2026-10-05).
      /// \\param[in] _ecm Mutable Entity-component manager.
      /// \\param[in] _addedMass 6x6 added-mass matrix, row-major. Units match
      /// the SDF `<fluid_added_mass>` element (kg for the top-left 3x3 block,
      /// kg*m^2 for the bottom-right 3x3 block).
      public: void SetFluidAddedMass(EntityComponentManager &_ecm,
                                     const math::Matrix6d &_addedMass) const;

      /// \\brief By default, Gazebo will not report bounding box for a link, so"""

# ---------------------------------------------------------------- 2) Link.cc
LINK_CC_INC_OLD = """#include "gz/sim/components/ExternalWorldWrenchCmd.hh"
"""
LINK_CC_INC_NEW = """#include "gz/sim/components/ExternalWorldWrenchCmd.hh"
#include "gz/sim/components/ExternalFluidAddedMassCmd.hh"
"""

LINK_CC_IMPL_OLD = """    msgs::Set(linkWrenchComp->Data().mutable_torque(),
      msgs::Convert(linkWrenchComp->Data().torque()) + torqueWithOffset);
  }
}

//////////////////////////////////////////////////
void Link::EnableBoundingBoxChecks("""

LINK_CC_IMPL_NEW = """    msgs::Set(linkWrenchComp->Data().mutable_torque(),
      msgs::Convert(linkWrenchComp->Data().torque()) + torqueWithOffset);
  }
}

//////////////////////////////////////////////////
void Link::SetFluidAddedMass(EntityComponentManager &_ecm,
                             const math::Matrix6d &_addedMass) const
{
  // Flatten to row-major 36 doubles (the component stores a std::vector<double>
  // so that it can reuse the existing VectorDoubleSerializer).
  std::vector<double> flat(36, 0.0);
  for (std::size_t r = 0; r < 6; ++r)
  {
    for (std::size_t c = 0; c < 6; ++c)
    {
      flat[r * 6 + c] = _addedMass(r, c);
    }
  }

  auto comp =
      _ecm.Component<components::ExternalFluidAddedMassCmd>(this->dataPtr->id);
  if (comp)
  {
    comp->Data() = flat;
  }
  else
  {
    _ecm.CreateComponent(this->dataPtr->id,
        components::ExternalFluidAddedMassCmd(flat));
  }
}

//////////////////////////////////////////////////
void Link::EnableBoundingBoxChecks("""

# ------------------------------------------------------------ 3) Physics.cc
PHY_INC_OLD = """#include "gz/sim/components/ExternalWorldWrenchCmd.hh"
"""
PHY_INC_NEW = """#include "gz/sim/components/ExternalWorldWrenchCmd.hh"
#include "gz/sim/components/ExternalFluidAddedMassCmd.hh"
"""

PHY_FL_OLD = """  //////////////////////////////////////////////////
  // Link force
  /// \\brief Feature list for applying forces to links.
  public: struct LinkForceFeatureList : physics::FeatureList<
            physics::AddLinkExternalForceTorque>{};
"""
PHY_FL_NEW = """  //////////////////////////////////////////////////
  // Link force
  /// \\brief Feature list for applying forces to links.
  public: struct LinkForceFeatureList : physics::FeatureList<
            physics::AddLinkExternalForceTorque>{};

  //////////////////////////////////////////////////
  // Added mass (KeShip, 2026-10-05)
  /// \\brief Feature list for setting a link's fluid added mass at runtime.
  public: struct AddedMassFeatureList : physics::FeatureList<
            physics::AddedMass>{};
"""

PHY_MAP_OLD = """  public: using EntityLinkMap = EntityFeatureMap3d<
            physics::Link,
            MinimumFeatureList,
            DetachableJointFeatureList,
            CollisionFeatureList,
            HeightmapFeatureList,
            LinkForceFeatureList,
            MeshFeatureList,
            LinkBoundingBoxFeatureList>;"""

PHY_MAP_NEW = """  public: using EntityLinkMap = EntityFeatureMap3d<
            physics::Link,
            MinimumFeatureList,
            DetachableJointFeatureList,
            CollisionFeatureList,
            HeightmapFeatureList,
            LinkForceFeatureList,
            MeshFeatureList,
            LinkBoundingBoxFeatureList,
            AddedMassFeatureList>;"""

PHY_USE_OLD = """        math::Vector3 force = msgs::Convert(_wrenchComp->Data().force());
        math::Vector3 torque = msgs::Convert(_wrenchComp->Data().torque());
        linkForceFeature->AddExternalForce(math::eigen3::convert(force));
        linkForceFeature->AddExternalTorque(math::eigen3::convert(torque));

        return true;
      });
"""

PHY_USE_NEW = """        math::Vector3 force = msgs::Convert(_wrenchComp->Data().force());
        math::Vector3 torque = msgs::Convert(_wrenchComp->Data().torque());
        linkForceFeature->AddExternalForce(math::eigen3::convert(force));
        linkForceFeature->AddExternalTorque(math::eigen3::convert(torque));

        return true;
      });

  // Fluid added mass commands (KeShip, 2026-10-05)
  // Forwards the commanded 6x6 matrix to gz-physics so the link's added mass
  // can change at runtime. Nothing writes this component unless a plugin
  // explicitly asks for it, so this loop is a no-op by default.
  _ecm.Each<components::ExternalFluidAddedMassCmd>(
      [&](const Entity &_entity,
          const components::ExternalFluidAddedMassCmd *_maComp)
      {
        if (!this->entityLinkMap.HasEntity(_entity))
        {
          return true;
        }

        auto linkAddedMassFeature =
            this->entityLinkMap.EntityCast<AddedMassFeatureList>(_entity);
        if (!linkAddedMassFeature)
        {
          static bool informed{false};
          if (!informed)
          {
            gzdbg << "Attempting to set the fluid added mass, but the physics "
                   << "engine doesn't support feature [AddedMass]. Command will "
                   << "be ignored." << std::endl;
            informed = true;
          }
          return false;
        }

        const auto &flat = _maComp->Data();
        if (flat.size() != 36u)
        {
          gzerr << "ExternalFluidAddedMassCmd must hold 36 values, got "
                << flat.size() << ". Ignoring." << std::endl;
          return true;
        }

        math::Matrix6d m;
        for (std::size_t r = 0; r < 6; ++r)
        {
          for (std::size_t c = 0; c < 6; ++c)
          {
            m(r, c) = flat[r * 6 + c];
          }
        }
        linkAddedMassFeature->SetAddedMass(m);

        return true;
      });
"""


def main():
    print("== [1] Link.hh ==")
    if not apply(SRC / "include/gz/sim/Link.hh",
                 [(LINK_HH_OLD, LINK_HH_NEW)], "Link.hh"):
        return 1
    print("  [OK]")

    print("== [2] Link.cc ==")
    if not apply(SRC / "src/Link.cc",
                 [(LINK_CC_INC_OLD, LINK_CC_INC_NEW),
                  (LINK_CC_IMPL_OLD, LINK_CC_IMPL_NEW)], "Link.cc"):
        return 1
    print("  [OK]")

    print("== [3] Physics.cc ==")
    if not apply(SRC / "src/systems/physics/Physics.cc",
                 [(PHY_INC_OLD, PHY_INC_NEW),
                  (PHY_FL_OLD, PHY_FL_NEW),
                  (PHY_MAP_OLD, PHY_MAP_NEW),
                  (PHY_USE_OLD, PHY_USE_NEW)], "Physics.cc"):
        return 1
    print("  [OK]")

    print("== 全部补丁完成（备份后缀 .bak_s1）==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
