//------------------------------------------------------------------------------------------------
//! One owner-predicted aero step. Seq matches RpcAsk_MHJ_Steer so a pose
//! packet can drop every sample the server has already applied.
//------------------------------------------------------------------------------------------------
class MHJ_InputSample
{
	int m_iSeq;
	float m_fTurn;
	float m_fPitch;
	float m_fDt;
}
