//------------------------------------------------------------------------------------------------
//! Skip landing/ragdoll fall damage while a HALO command is active.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterDamageManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnHandleFallDamage(EFallDamageType fallDamageType, vector velocityVector)
	{
		IEntity owner = GetOwner();
		if (owner)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(owner.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler && handler.MHJ_IsHaloJumping())
				return;
		}

		super.OnHandleFallDamage(fallDamageType, velocityVector);
	}
}
