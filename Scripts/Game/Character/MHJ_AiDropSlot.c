//------------------------------------------------------------------------------------------------
//! One AI jumper owned by MHJ_AiDropDirector. Server flight state only.
//------------------------------------------------------------------------------------------------
class MHJ_AiDropSlot
{
	ChimeraCharacter m_Character;
	RplId m_JumperId;
	vector m_vOrigin;
	vector m_vVel;
	vector m_vWind;
	vector m_vLz;
	float m_fHeading;
	float m_fPathDeg;
	float m_fPathDegV;
	float m_fTurnFilt;
	float m_fTurnFiltV;
	float m_fPitchFilt;
	float m_fPitchFiltV;
	float m_fOpenT;
	float m_fSimT;
	bool m_bActive;
}
