//------------------------------------------------------------------------------------------------
//! Local-only smoothing for an AI canopy mesh. The pawn transform is a standing
//! infantry pose, so the visual cannot copy GetWorldTransform.
//------------------------------------------------------------------------------------------------
class MHJ_AiCanopyVisualPose
{
	vector m_vPrevOrigin;
	float m_fYaw;
	float m_fPitch;
	float m_fBank;
	float m_fYawV;
	float m_fPitchV;
	float m_fBankV;
	bool m_bSeeded;

	//------------------------------------------------------------------------------------------------
	void Apply(vector origin, float wantYaw, float wantPitch, float wantBank, float dt, out vector ypr)
	{
		if (!m_bSeeded)
		{
			m_fYaw = wantYaw;
			m_fPitch = wantPitch;
			m_fBank = wantBank;
			m_vPrevOrigin = origin;
			m_bSeeded = true;
		}
		else
		{
			if (dt < 0.001)
				dt = 0.001;

			float yawTarget = m_fYaw + WrapDeg(wantYaw - m_fYaw);
			m_fYaw = Math.SmoothCD(m_fYaw, yawTarget, m_fYawV, MHJ_Constants.CANOPY_BANK_INERTIA, 90, dt);
			m_fPitch = Math.SmoothCD(m_fPitch, wantPitch, m_fPitchV, MHJ_Constants.CANOPY_PITCH_INERTIA, 120, dt);
			m_fBank = Math.SmoothCD(m_fBank, wantBank, m_fBankV, MHJ_Constants.CANOPY_BANK_INERTIA, 80, dt);
			m_vPrevOrigin = origin;
		}

		ypr[0] = m_fYaw;
		ypr[1] = m_fPitch;
		ypr[2] = m_fBank;
	}

	//------------------------------------------------------------------------------------------------
	static float WrapDeg(float deg)
	{
		while (deg > 180)
			deg = deg - 360;
		while (deg < -180)
			deg = deg + 360;
		return deg;
	}
}
