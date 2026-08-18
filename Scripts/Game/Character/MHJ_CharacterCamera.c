//------------------------------------------------------------------------------------------------
//! After canopy GetOut, leftover vehicle cameras and PhysicsIsLinked both set
//! UseHeading = 0, so mouse yaw only turns the head. Force heading whenever
//! we are not actually seated.
//------------------------------------------------------------------------------------------------
class MHJ_LookRestore
{
	//------------------------------------------------------------------------------------------------
	static bool ShouldForceHeading(ChimeraCharacter ch)
	{
		if (!ch)
			return false;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (!access)
			return true;
		if (access.IsInCompartment())
			return false;
		if (access.IsGettingIn())
			return false;
		return true;
	}
}

//------------------------------------------------------------------------------------------------
modded class CharacterCamera1stPerson
{
	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (CharacterCamera1stPersonVehicle.Cast(this))
			return;
		if (CharacterCamera1stPersonBoneTransform.Cast(this))
			return;
		if (!MHJ_LookRestore.ShouldForceHeading(m_OwnerCharacter))
			return;

		pOutResult.m_fUseHeading = 1.0;
	}
}

//------------------------------------------------------------------------------------------------
modded class CharacterCamera1stPersonVehicle
{
	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (!MHJ_LookRestore.ShouldForceHeading(m_OwnerCharacter))
			return;

		pOutResult.m_fUseHeading = 1.0;
	}
}

//------------------------------------------------------------------------------------------------
modded class CharacterCamera3rdPersonBase
{
	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (!MHJ_LookRestore.ShouldForceHeading(m_OwnerCharacter))
			return;

		pOutResult.m_fUseHeading = 1.0;
	}
}

//------------------------------------------------------------------------------------------------
modded class CharacterCamera3rdPersonVehicle
{
	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (!MHJ_LookRestore.ShouldForceHeading(m_OwnerCharacter))
			return;

		pOutResult.m_fUseHeading = 1.0;
	}
}
