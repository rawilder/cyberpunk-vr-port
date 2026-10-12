// CyberpunkVRPort — hand-anchored weapon wheel (redscript side).
//
// The wheel itself is drawn by the plugin as a quad anchored to the right hand (src/Overlay/
// HandWheel.cpp) and driven by hand displacement. This file answers the two questions the CET
// side cannot: what is in each weapon slot (for the labels), and the equip request when a
// slot is chosen -- the same EquipmentSystem path the holster module and the vanilla holster
// button use, so the draw animation, ammo and HUD all behave.

@addMethod(PlayerPuppet)
public func VRWheelSlotLabel(slot: Int32) -> String {
  let data = EquipmentSystem.GetData(this);
  if !IsDefined(data) { return ""; }
  let item = data.GetItemInEquipSlot(gamedataEquipmentArea.Weapon, slot);
  if !ItemID.IsValid(item) { return ""; }
  let record = TweakDBInterface.GetItemRecord(ItemID.GetTDBID(item));
  if !IsDefined(record) { return ""; }
  return GetLocalizedText(record.DisplayName());
}

@addMethod(PlayerPuppet)
public func VRWheelEquip(slot: Int32) -> Void {
  let req = new EquipmentSystemWeaponManipulationRequest();
  if slot == 0 { req.requestType = EquipmentManipulationAction.RequestWeaponSlot1; }
  else if slot == 1 { req.requestType = EquipmentManipulationAction.RequestWeaponSlot2; }
  else if slot == 2 { req.requestType = EquipmentManipulationAction.RequestWeaponSlot3; }
  else { req.requestType = EquipmentManipulationAction.UnequipWeapon; }
  req.owner = this;
  let eqSystem = GameInstance.GetScriptableSystemsContainer(this.GetGame()).Get(n"EquipmentSystem") as EquipmentSystem;
  if IsDefined(eqSystem) { eqSystem.QueueRequest(req); }
}
