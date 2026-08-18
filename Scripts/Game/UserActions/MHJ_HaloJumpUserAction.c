//------------------------------------------------------------------------------------------------
//! Sign action. Local only — opens the HALO map menu on the user who used it.
//------------------------------------------------------------------------------------------------
class MHJ_HaloJumpUserAction : ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
		if (!localChar)
			return;
		if (localChar != pUserEntity)
			return;

		if (MHJ_HaloJumpMenu.IsOpen())
			return;

		MHJ_HaloJumpMenu.Open();
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return CanBePerformedScript(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!user)
			return false;

		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (!character)
			return false;

		if (character.IsInVehicle())
			return false;

		if (MHJ_HaloJumpMenu.IsOpen())
			return false;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(character.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler && handler.MHJ_IsHaloJumping())
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		outName = "HALO Jump";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBroadcastScript()
	{
		return false;
	}
}
