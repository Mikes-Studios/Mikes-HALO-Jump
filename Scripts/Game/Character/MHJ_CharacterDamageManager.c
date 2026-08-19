//------------------------------------------------------------------------------------------------
//! Skip HALO/canopy leftover landing and collision fall damage until the pawn
//! has been on foot long enough for native Fall to forget jump altitude.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterDamageManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnHandleFallDamage(EFallDamageType fallDamageType, vector velocityVector)
	{
		if (MHJ_ShouldIgnoreFallDamage())
		{
			MHJ_Log.Warning("Ignored native fall damage type=" + fallDamageType.ToString() + " speed=" + velocityVector.Length().ToString());
			return;
		}

		super.OnHandleFallDamage(fallDamageType, velocityVector);
	}

	//------------------------------------------------------------------------------------------------
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		if (damageContext.damageType == EDamageType.COLLISION && damageContext.struckHitZone == GetDefaultHitZone())
		{
			if (MHJ_ShouldIgnoreFallDamage())
			{
				MHJ_Log.Warning("Ignored collision fall hijack value=" + damageContext.damageValue.ToString());
				return true;
			}
		}

		return super.HijackDamageHandling(damageContext);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_ShouldIgnoreFallDamage()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return false;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(owner.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (!handler)
			return false;
		return handler.MHJ_ShouldIgnoreFallDamage();
	}
}
