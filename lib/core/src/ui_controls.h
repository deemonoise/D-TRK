#pragma once
namespace mt {
// UI explanations do not decide editability: dim rows may still be configured for later use.
inline const char* parameterHint(bool inactive, bool editable, const char* reason) {
  if (inactive) return reason && reason[0] ? reason : "INACTIVE WITH CURRENT SETTINGS";
  return editable ? nullptr : "READ ONLY";
}
}
