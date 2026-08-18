//------------------------------------------------------------------------------------------------
//! After canopy GetOut, ChimeraCharacter.IsInVehicle() can stay true with no seat.
//! Vanilla then keeps CHARACTERCAMERA_3RD_VEHICLE, which sets UseHeading = 0 so
//! mouse look never yaws the pawn. Force on-foot cameras when we are not seated.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterCameraHandlerComponent
{
	//------------------------------------------------------------------------------------------------
	override int CameraSelector()
	{
		int cam = super.CameraSelector();

		ChimeraCharacter ch = ChimeraCharacter.Cast(GetOwner());
		if (!ch)
			return cam;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (access)
		{
			if (access.IsInCompartment())
				return cam;
			if (access.IsGettingIn())
				return cam;
			// Do not keep 1ST_VEHICLE_TRANSITION just because IsGettingOut() is stuck.
			// After canopy GetOut that flag can stay true with no seat, which locks
			// UseHeading at 0 so mouse look never yaws the pawn.
		}

		if (!MHJ_IsVehicleCamera(cam))
			return cam;

		if (IsInThirdPerson())
			return CharacterCameraSet.CHARACTERCAMERA_3RD_ERC;
		return CharacterCameraSet.CHARACTERCAMERA_1ST;
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_IsVehicleCamera(int cam)
	{
		if (cam == CharacterCameraSet.CHARACTERCAMERA_3RD_VEHICLE)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_1ST_VEHICLE)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_1ST_VEHICLE_TRANSITION)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_ADS_VEHICLE)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_3RD_TURRET)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_1ST_TURRET)
			return true;
		if (cam == CharacterCameraSet.CHARACTERCAMERA_1ST_TURRET_TRANSITION)
			return true;
		return false;
	}
}
