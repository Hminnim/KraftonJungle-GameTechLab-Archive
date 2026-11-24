# [W11] 언리얼 스타일 AnimGraph, 1D BlendSpace, AnimNotify & 전용 에디터

- **기간**: 2026-11-05 ~ 11-11
- **원본 레포지토리 / 브랜치**: `Hminnim/Techlab4_Week11_Team7` (`main`)

## 본인 구현 내역

- `UAnimInstance`, `FAnimNodeBase`, `FAnimNode_SequencePlayer`, `FAnimNode_TwoWayBlend`, `FAnimNode_BlendSpacePlayer`, `FQuat` Slerp 구현
- 1D `UBlendSpace` 가중치 블렌딩 및 `AnimNotify` / `AnimNotifyState` Lua 스크립트 바인딩(`LuaBindings`)
- `AnimationSequenceEditorPanel`(Notify 타임라인) 및 `AnimationBlendEditorPanel`(1,160+ LOC) 전용 ImGui 에디터 구현
