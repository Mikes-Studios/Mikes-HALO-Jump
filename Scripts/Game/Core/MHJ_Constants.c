//------------------------------------------------------------------------------------------------
//! Shared HALO numbers. Altitudes are metres above terrain at the drop.
//! Freefall is quadratic drag plus body lift. Canopy is a ram-air wing in the wind.
//------------------------------------------------------------------------------------------------
class MHJ_Constants
{
	static const float JUMP_ALT_MIN = 200;
	static const float JUMP_ALT_MAX = 3000;
	static const float JUMP_ALT_DEFAULT = 500;

	static const float OPEN_ALT_MIN = 80;
	static const float OPEN_ALT_MAX = 1500;
	static const float OPEN_ALT_DEFAULT = 250;

	static const float OPEN_MARGIN = 50;
	static const float LAND_AGL = 1.6;
	//! Sanity gap from authority craft to owner-reported ground. Reconciled
	//! prediction should keep this to metres; 40 m is a cheat / desync cap.
	static const float CANOPY_OWNER_TOUCHDOWN_AGL = 40;
	static const float FLARE_AGL = 20;
	static const float FLARE_END_AGL = 2.5;
	static const float LAND_HARD_SINK = 8;
	static const float LAND_FATAL_SINK = 18;
	static const float LAND_CARRY_MIN = 2.5;
	static const float LAND_CARRY_FULL = 10;
	static const float LAND_CARRY_ANALOG_MIN = 0.55;
	static const float LAND_CARRY_ANALOG_MAX = 1.15;
	static const float LAND_CARRY_TIME = 1.0;
	static const float LAND_CARRY_INPUT_BREAK = 0.2;
	static const int FALL_DAMAGE_IGNORE_MS = 15000;

	static const float GRAVITY = 9.81;
	//! Matches MHJ_DeployedCanopy RigidBody mass. Polar cruise/dive is tuned for this.
	static const float MASS = 200;
	static const float AIR_DENSITY_SL = 1.325;
	static const float BODY_AREA = 0.55;

	static const float FREEFALL_TERMINAL = 54;
	static const float FREEFALL_SLOW_TERMINAL = 45;
	static const float FREEFALL_TRACK_TERMINAL = 58;
	static const float FREEFALL_TRACK_LIFT = 1.15;
	static const float FREEFALL_TRACK_BASE = 0.42;
	static const float FREEFALL_SLIDE = 0.45;
	static const float FREEFALL_TURN_RATE = 1.35;
	static const float FREEFALL_TURN_ALIGN = 2.8;
	static const float FREEFALL_BANK_MAX = 34;
	//! Extra craft pitch while the jumper is in the belly-pitched freefall seat.
	static const float FREEFALL_PITCH_ARCH = 0;
	static const float FREEFALL_PITCH_TRACK = -16;
	static const float FREEFALL_PITCH_SLOW = 12;
	static const float FREEFALL_START_SINK = 8;
	static const int FREEFALL_INPUT_HOLD_MS = 250;
	//! Body and canopy path never go past this. 90° straight down gimbal-flips look and lift.
	static const float DIVE_ANGLE_MAX = 85;
	static const float FREEFALL_EXIT_TIME = 1.15;
	static const float FREEFALL_MAX_SPEED = 90;

	static const float CANOPY_AREA = 23;
	static const float CANOPY_STALL_SPEED = 6.4;
	static const float CANOPY_CRUISE_TAS = 11;
	static const float CANOPY_MAX_TAS = 52;
	//! Working MK4 m_MaxFallSpeed. Hands-off canopy bleeds toward this, never clips.
	static const float CANOPY_MAX_SINK = 5;
	//! m/s² toward MAX_SINK. 5 matches working HandleDrag. Opening may dump faster.
	static const float CANOPY_SINK_DECEL = 5;
	static const float CANOPY_OPEN_SINK_DECEL = 18;
	static const int CANOPY_DELETE_DELAY_MS = 200;
	//! Working MK4 deletes 200 ms after GetOut. Wait extra if occupancy is still set
	//! so PerceivableComponent.IsInCompartment() cannot stick after the craft dies.
	static const int CANOPY_EXIT_POLL_MS = 50;
	static const int CANOPY_EXIT_MAX_TRIES = 40;
	//! Hands-off polar. 0.55 / 0.15 flew too flat and hung at ~3.4 m/s sink.
	static const float CANOPY_CL_TRIM = 0.42;
	static const float CANOPY_CD_TRIM = 0.17;
	static const float CANOPY_CL_DIVE_DROP = 0.50;
	static const float CANOPY_CL_BRAKE_ADD = 0.48;
	static const float CANOPY_CD_DIVE_ADD = -0.12;
	static const float CANOPY_CD_BRAKE_ADD = 0.18;
	static const float CANOPY_CL_ENERGY = 0.62;
	//! Hands-off high TAS keeps a dive-like CD so a released dive coasts instead of slamming to trim.
	static const float CANOPY_CD_ENERGY_DROP = 0.10;
	static const float CANOPY_DIVE_ACCEL = 14;
	static const float CANOPY_MAX_CLIMB = 2.2;
	static const float CANOPY_TURN_RATE = 0.95;
	static const float CANOPY_HEADING_LERP = 2.5;
	static const float CANOPY_BANK_MAX = 32;
	static const float CANOPY_BANK_INERTIA = 0.38;
	static const float CANOPY_PITCH_CRUISE = -24;
	static const float CANOPY_PITCH_DIVE = -85;
	static const float CANOPY_PITCH_FLARE = 16;
	static const float CANOPY_PITCH_SNATCH = 0;
	static const float CANOPY_PITCH_INERTIA = 0.45;
	//! Path angles must match visual pitch in cruise/dive so the wing flies where it points.
	static const float CANOPY_PATH_CRUISE = -24;
	static const float CANOPY_PATH_DIVE = -85;
	static const float CANOPY_PATH_FLARE = -2;
	static const float CANOPY_PATH_ALIGN = 2.4;
	static const float CANOPY_PATH_ALIGN_DIVE = 2.4;
	static const float CANOPY_PATH_ALIGN_FLARE = 2.0;
	static const float CANOPY_PATH_INERTIA = 0.42;
	static const float CANOPY_PATH_IN_MAX = 70;
	//! Excess TAS toward cruise after the path has flattened. 0.16 is ~6 s; 1.1 dumped a dive in one second.
	static const float CANOPY_RELEASE_BLEED = 0.16;
	static const float CANOPY_HANG_LENGTH = 4.2;
	static const float CANOPY_HANG_INERTIA = 0.42;
	//! World-up metres from the standing pawn's feet to the ParachuteMK3.xob
	//! origin. That origin is the craft / harness, not the fabric — the player
	//! sit slot is +1.474 above it, and the canopy is several metres above
	//! that. Positive pushes the fabric farther from the AI. Negative puts
	//! the AI into the harness. -1.474 parks feet on the sit slot.
	static const float CANOPY_AI_VISUAL_Y = -0.474;
	static const float CANOPY_STRETCH_TIME = 0.32;
	static const float CANOPY_INFLATE_TIME = 1.05;
	static const float CANOPY_OPEN_TIME = 2.4;
	static const float CANOPY_SNATCH_OVERSHOOT = 1.18;
	static const float CANOPY_PITCH_IN_TIME = 0.82;
	static const float CANOPY_PITCH_IN_MAX = 3;
	static const float CANOPY_DIVE_IN_TIME = 1.42;
	static const float CANOPY_DIVE_IN_MAX = 1.8;
	//! Dedicated clients wait for the server canopy to replicate before GetIn.
	static const float CANOPY_BOARD_WAIT = 12;
	//! Pose RPC and owner steer sample rate. Hard-applying this origin every
	//! packet rubberbands by speed×RTT; the owner replays unacked samples instead.
	static const float FLIGHT_STATE_DT = 0.033;
	//! Zero server sticks if no owner steer arrives within this window.
	static const float INPUT_STALE_SEC = 0.3;
	//! Owner input history kept for snapshot replay. ~1.5 s at 60 Hz.
	static const int INPUT_RING_MAX = 96;
	//! Visual offset from a replay correction decays over this many seconds.
	static const float REPLICA_VISUAL_ERROR_TIME = 0.3;
	static const float REPLICA_SNAP_AGE_MAX = 0.25;

	static const float WIND_REF_HEIGHT = 10;
	static const float WIND_SHEAR_EXP = 0.11;
	static const float WIND_SHEAR_MAX = 2.2;
	static const float WIND_GUST = 0.12;
	static const float WIND_TURB = 0.08;
	//! Weather wind used by freefall and canopy. 0.25 = one quarter of engine wind.
	static const float WIND_AERO_SCALE = 0.25;

	static const float STEER_FILTER_TIME = 0.2;
	static const float STEER_FILTER_MAX = 90;

	static const float WORLD_SIZE_FALLBACK = 12800;

	static const string CANOPY_PREFAB = "{C4E8A27B1F906D90}Prefabs/MHJ_DeployedCanopy.et";
	static const string CANOPY_MESH = "{A372F4DA63729C28}Assets/ParachuteMK3/ParachuteMK3.xob";
	static const string CANOPY_MAT_CHUTE = "{85583F983D5A2BA8}Assets/ParachuteMK3/Data/Chute.emat";
	static const string CANOPY_MAT_LINE = "{31106DE7805868A1}Assets/ParachuteMK3/Data/Line.emat";
	static const string CANOPY_MESH_REMAP = "$remap 'Chute' '{85583F983D5A2BA8}Assets/ParachuteMK3/Data/Chute.emat'; $remap 'Line' '{31106DE7805868A1}Assets/ParachuteMK3/Data/Line.emat'; $remap '01__Default' '{17C975C41F8929BD}Assets/ParachuteMK3/Data/01__Default.emat'";
	static const float AI_DROP_AGL = 380;
	static const int AI_DROP_MAX = 40;
	//! Applied only to the AI origin step. Do not write these back into m_vVel.
	static const float AI_CANOPY_SPEED_SCALE = 0.5;
	static const float AI_CANOPY_SINK_SCALE = 2;
	//! Height the open-sky check traces down from. 1.9 m stand room alone still
	//! passes interiors.
	static const float AI_DROP_SKY_M = 12;
	static const float AI_LAND_STAND_M = 1.9;
	static const float AI_LAND_BIAS_M = 0.1;
	static const float AI_LAND_SEARCH_M = 16;
	static const float AI_GROUND_TRACE_M = 100;
	//! Swept collision for the AI origin step. SetOrigin never collides, so the
	//! step is traced as a torso sphere: centre AI_SWEEP_CHEST_M above the feet
	//! so terrain under a descending jumper does not block forward flight.
	static const float AI_SWEEP_R_M = 0.4;
	static const float AI_SWEEP_CHEST_M = 1.0;
	static const float AI_SWEEP_SKIN_M = 0.12;
	//! Down-trace start offset above the pre-step feet. Starting on the surface
	//! can report the collider the jumper is already resting on.
	static const float AI_SWEEP_UP_M = 0.5;
	static const string SLOT_FREEFALL = "freefall";
	static const string SLOT_CANOPY = "passenger_l02";
	static const int CRAFT_INTERACTION_LAYER = 0x4040;
	//! GetInVehicle forceTeleport ignores this; -1 matches vanilla teleport GetIn.
	static const int GETIN_DOOR_ANY = -1;
}

//------------------------------------------------------------------------------------------------
enum MHJ_EHaloPhase
{
	FREEFALL,
	CANOPY,
	LANDED
}
