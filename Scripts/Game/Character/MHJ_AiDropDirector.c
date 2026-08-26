//------------------------------------------------------------------------------------------------
//! Server-authoritative AI drop stick. One script-spawned entity drives every
//! jumper with autopilot. Clients spawn local-only canopy meshes from jumper
//! RplIds. Do not use a hand-authored prefab GUID — Workbench never registered
//! those, so RPL insert failed and the visual remaps died.
//------------------------------------------------------------------------------------------------
class MHJ_AiDropDirectorClass : GenericEntityClass
{
}

class MHJ_AiDropDirector : GenericEntity
{
	protected static MHJ_AiDropDirector s_Live;
	protected static ref array<RplId> s_aRemoteIds;
	protected static ref array<IEntity> s_aRemoteVisuals;
	protected static ref array<ref MHJ_AiCanopyVisualPose> s_aRemotePoses;
	//! Unreliable pose packets can arrive after the reliable land remove.
	//! Those IDs stay here so a late pose cannot spawn a new mesh on a walker.
	protected static ref array<RplId> s_aFinishedIds;
	protected static bool s_bRemoteTick;

	protected ref array<ref MHJ_AiDropSlot> m_aSlots;
	protected ref array<RplId> m_aClientIds;
	protected ref array<IEntity> m_aVisuals;
	protected ref array<ref MHJ_AiCanopyVisualPose> m_aVisualPoses;
	protected ref ScriptInvoker m_OnJumperFinished;
	protected bool m_bClosing;
	protected float m_fPoseSendTime;
	protected static bool s_bLoggedVisualY;

	//------------------------------------------------------------------------------------------------
	void MHJ_AiDropDirector(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	void ~MHJ_AiDropDirector(IEntitySource src, IEntity parent)
	{
		ClearClientVisuals();
		if (s_Live == this)
			s_Live = null;
	}

	//------------------------------------------------------------------------------------------------
	static MHJ_AiDropDirector GetLive()
	{
		return s_Live;
	}

	//------------------------------------------------------------------------------------------------
	static bool HasLiveStick()
	{
		if (!s_Live)
			return false;
		return s_Live.HasActiveSlots();
	}

	//------------------------------------------------------------------------------------------------
	static bool IsActiveJumper(IEntity character)
	{
		if (!s_Live || !character)
			return false;
		return s_Live.FindSlot(character) != null;
	}

	//------------------------------------------------------------------------------------------------
	static MHJ_AiDropDirector SpawnStick(vector lz)
	{
		if (!Replication.IsServer())
			return null;
		if (s_Live && s_Live.HasActiveSlots())
		{
			MHJ_Log.Warning("AI drop stick refused: one stick is already live");
			return null;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = lz;

		MHJ_AiDropDirector director = MHJ_AiDropDirector.Cast(GetGame().SpawnEntity(MHJ_AiDropDirector, world, spawnParams));
		if (!director)
			return null;

		s_Live = director;
		MHJ_Log.Info("AI drop stick spawned at " + lz.ToString());
		return director;
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnJumperFinished()
	{
		if (!m_OnJumperFinished)
			m_OnJumperFinished = new ScriptInvoker();
		return m_OnJumperFinished;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (SCR_Global.IsEditMode())
			return;

		m_aSlots = new array<ref MHJ_AiDropSlot>();
		m_aClientIds = new array<RplId>();
		m_aVisuals = new array<IEntity>();
		m_aVisualPoses = new array<ref MHJ_AiCanopyVisualPose>();

		if (Replication.IsServer())
			s_Live = this;
	}

	//------------------------------------------------------------------------------------------------
	bool AddJumper(notnull ChimeraCharacter jumper, vector lz)
	{
		if (!Replication.IsServer())
			return false;
		if (ActiveSlotCount() >= MHJ_Constants.AI_DROP_MAX)
		{
			MHJ_Log.Warning("AI drop stick full");
			return false;
		}
		if (FindSlot(jumper))
			return false;

		RplComponent jumperRpl = RplComponent.Cast(jumper.FindComponent(RplComponent));
		if (!jumperRpl)
		{
			MHJ_Log.Warning("AI drop jumper has no RplComponent");
			return false;
		}

		RplId jumperId = jumperRpl.Id();
		if (!jumperId.IsValid())
		{
			MHJ_Log.Warning("AI drop jumper RplId invalid");
			return false;
		}

		ref MHJ_AiDropSlot slot = new MHJ_AiDropSlot();
		MHJ_AiDropAutopilot.InitSlot(slot, jumper, lz);
		slot.m_JumperId = jumperId;
		PrepareJumper(jumper);
		m_aSlots.Insert(slot);
		AddClientId(jumperId);
		RelayAdd(jumperId, SlotVisualYpr(slot));
		MHJ_Log.Info("AI drop jumper added id=" + jumperId.ToString());
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool HasActiveSlots()
	{
		return ActiveSlotCount() > 0;
	}

	//------------------------------------------------------------------------------------------------
	protected int ActiveSlotCount()
	{
		if (!m_aSlots)
			return 0;

		int count = m_aSlots.Count();
		int active = 0;
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (slot && slot.m_bActive)
				active = active + 1;
		}
		return active;
	}

	//------------------------------------------------------------------------------------------------
	protected MHJ_AiDropSlot FindSlot(IEntity character)
	{
		if (!m_aSlots || !character)
			return null;

		int count = m_aSlots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (slot && slot.m_bActive && slot.m_Character == character)
				return slot;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (Replication.IsServer())
			TickAuthority(timeSlice);
		TickClientVisuals();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickAuthority(float timeSlice)
	{
		if (!m_aSlots)
			return;

		int count = m_aSlots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (!slot || !slot.m_bActive)
				continue;
			TickSlot(slot, timeSlice);
		}

		RelayPoses(timeSlice);

		if (!m_bClosing && ActiveSlotCount() <= 0 && count > 0)
			BeginClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickSlot(notnull MHJ_AiDropSlot slot, float dt)
	{
		ChimeraCharacter jumper = slot.m_Character;
		if (!jumper)
		{
			FinishSlot(slot, null);
			return;
		}

		CharacterControllerComponent controller = jumper.GetCharacterController();
		if (controller && controller.IsDead())
		{
			RestoreJumper(jumper, false);
			FinishSlot(slot, jumper);
			return;
		}

		MHJ_AiDropAutopilot.Step(slot, dt);
		ApplySlotToJumper(slot);

		float agl = MHJ_AiDropAutopilot.GetAgl(slot.m_vOrigin, jumper);
		if (agl <= MHJ_Constants.LAND_AGL)
		{
			LandSlot(slot);
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplySlotToJumper(notnull MHJ_AiDropSlot slot)
	{
		ChimeraCharacter jumper = slot.m_Character;
		if (!jumper)
			return;

		Physics physics = jumper.GetPhysics();
		if (physics)
		{
			physics.EnableGravity(false);
			physics.SetVelocity(vector.Zero);
			physics.SetAngularVelocity(vector.Zero);
		}

		jumper.SetOrigin(slot.m_vOrigin);
		vector ypr;
		ypr[0] = slot.m_fHeading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;
		jumper.SetYawPitchRoll(ypr);
	}

	//------------------------------------------------------------------------------------------------
	protected void LandSlot(notnull MHJ_AiDropSlot slot)
	{
		ChimeraCharacter jumper = slot.m_Character;
		vector origin = slot.m_vOrigin;
		float groundY = MHJ_AiDropAutopilot.GroundY(origin, jumper);
		origin[1] = groundY + MHJ_Constants.AI_LAND_BIAS_M;

		vector openPos;
		if (MHJ_AiDropAutopilot.TryFindOpenGround(origin, MHJ_Constants.AI_LAND_SEARCH_M, jumper, openPos))
		{
			if (openPos != origin)
				MHJ_Log.Info("AI drop relocated id=" + slot.m_JumperId.ToString() + " to " + openPos.ToString());
			origin = openPos;
		}

		slot.m_vOrigin = origin;
		slot.m_vVel = vector.Zero;

		if (jumper)
		{
			jumper.SetOrigin(origin);
			RestoreJumper(jumper, true);
		}

		MHJ_Log.Info("AI drop landed id=" + slot.m_JumperId.ToString());
		FinishSlot(slot, jumper);
	}

	//------------------------------------------------------------------------------------------------
	protected void PrepareJumper(notnull ChimeraCharacter jumper)
	{
		Physics physics = jumper.GetPhysics();
		if (physics)
			physics.EnableGravity(false);

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginAiDropIgnore();
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreJumper(notnull ChimeraCharacter jumper, bool landed)
	{
		Physics physics = jumper.GetPhysics();
		if (physics)
		{
			physics.EnableGravity(true);
			physics.SetVelocity(vector.Zero);
			physics.SetAngularVelocity(vector.Zero);
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginAiDropIgnore();
	}

	//------------------------------------------------------------------------------------------------
	protected void FinishSlot(notnull MHJ_AiDropSlot slot, IEntity jumper)
	{
		if (!slot.m_bActive)
			return;

		slot.m_bActive = false;
		RplId jumperId = slot.m_JumperId;
		RemoveClientId(jumperId);
		RelayRemove(jumperId);

		if (m_OnJumperFinished && jumper)
			m_OnJumperFinished.Invoke(jumper);
	}

	//------------------------------------------------------------------------------------------------
	protected void BeginClose()
	{
		if (m_bClosing)
			return;
		m_bClosing = true;
		GetGame().GetCallqueue().CallLater(DeleteStick, 400, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteStick()
	{
		if (!Replication.IsServer())
			return;
		if (s_Live == this)
			s_Live = null;

		ClearClientVisuals();
		SCR_EntityHelper.DeleteEntityAndChildren(this);
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayAdd(RplId jumperId, vector ypr)
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.MHJ_RelayAiDropAdd(jumperId, ypr);
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayRemove(RplId jumperId)
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.MHJ_RelayAiDropRemove(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayPoses(float timeSlice)
	{
		if (!m_aSlots)
			return;

		m_fPoseSendTime = m_fPoseSendTime + timeSlice;
		if (m_fPoseSendTime < MHJ_Constants.AI_VISUAL_POSE_DT)
			return;
		m_fPoseSendTime = 0;

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gameMode)
			return;

		int count = m_aSlots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (!slot || !slot.m_bActive)
				continue;
			if (!slot.m_JumperId.IsValid())
				continue;
			gameMode.MHJ_RelayAiDropPose(slot.m_JumperId, SlotVisualYpr(slot));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static vector SlotVisualYpr(notnull MHJ_AiDropSlot slot)
	{
		vector ypr;
		ypr[0] = slot.m_fHeading * Math.RAD2DEG;
		ypr[1] = slot.m_fPathDeg;
		ypr[2] = slot.m_fTurnFilt * MHJ_Constants.CANOPY_BANK_MAX;
		return ypr;
	}

	//------------------------------------------------------------------------------------------------
	protected void AddClientId(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return;
		if (!m_aClientIds)
			m_aClientIds = new array<RplId>();
		if (FindClientId(jumperId) >= 0)
			return;
		m_aClientIds.Insert(jumperId);
		if (!m_aVisuals)
			m_aVisuals = new array<IEntity>();
		m_aVisuals.Insert(null);
		if (!m_aVisualPoses)
			m_aVisualPoses = new array<ref MHJ_AiCanopyVisualPose>();
		ref MHJ_AiCanopyVisualPose pose = new MHJ_AiCanopyVisualPose();
		m_aVisualPoses.Insert(pose);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveClientId(RplId jumperId)
	{
		if (!m_aClientIds)
			return;

		int index = FindClientId(jumperId);
		if (index < 0)
			return;

		DestroyVisualAt(index);
		m_aClientIds.Remove(index);
		if (m_aVisuals && index < m_aVisuals.Count())
			m_aVisuals.Remove(index);
		if (m_aVisualPoses && index < m_aVisualPoses.Count())
			m_aVisualPoses.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	protected int FindClientId(RplId jumperId)
	{
		if (!m_aClientIds)
			return -1;

		int count = m_aClientIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			if (m_aClientIds[i] == jumperId)
				return i;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickClientVisuals()
	{
		DriveVisualList(m_aClientIds, m_aVisuals, m_aVisualPoses);
	}

	//------------------------------------------------------------------------------------------------
	static void ClientAddRemote(RplId jumperId, vector ypr)
	{
		if (!jumperId.IsValid())
			return;
		if (s_Live)
			return;
		if (IsFinishedId(jumperId))
			return;
		if (!s_aRemoteIds)
			s_aRemoteIds = new array<RplId>();
		if (!s_aRemoteVisuals)
			s_aRemoteVisuals = new array<IEntity>();
		if (!s_aRemotePoses)
			s_aRemotePoses = new array<ref MHJ_AiCanopyVisualPose>();

		int index = FindRemoteId(jumperId);
		if (index >= 0)
		{
			ApplyRelayAt(index, ypr);
			return;
		}

		s_aRemoteIds.Insert(jumperId);
		s_aRemoteVisuals.Insert(null);
		ref MHJ_AiCanopyVisualPose pose = new MHJ_AiCanopyVisualPose();
		pose.SetRelay(ypr[0], ypr[1], ypr[2]);
		s_aRemotePoses.Insert(pose);
		if (s_bRemoteTick)
			return;

		s_bRemoteTick = true;
		GetGame().GetCallqueue().CallLater(TickRemoteVisuals, 0, true);
	}

	//------------------------------------------------------------------------------------------------
	static void ClientApplyRelay(RplId jumperId, vector ypr)
	{
		if (!jumperId.IsValid())
			return;
		if (s_Live)
			return;
		if (IsFinishedId(jumperId))
			return;

		int index = FindRemoteId(jumperId);
		if (index < 0)
			return;

		ApplyRelayAt(index, ypr);
	}

	//------------------------------------------------------------------------------------------------
	protected static int FindRemoteId(RplId jumperId)
	{
		if (!s_aRemoteIds)
			return -1;

		int count = s_aRemoteIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			if (s_aRemoteIds[i] == jumperId)
				return i;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected static void ApplyRelayAt(int index, vector ypr)
	{
		if (!s_aRemotePoses || index < 0 || index >= s_aRemotePoses.Count())
			return;

		MHJ_AiCanopyVisualPose pose = s_aRemotePoses[index];
		if (!pose)
		{
			ref MHJ_AiCanopyVisualPose created = new MHJ_AiCanopyVisualPose();
			s_aRemotePoses[index] = created;
			pose = created;
		}
		pose.SetRelay(ypr[0], ypr[1], ypr[2]);
	}

	//------------------------------------------------------------------------------------------------
	static void ClientRemoveRemote(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return;

		MarkFinishedId(jumperId);

		if (!s_aRemoteIds)
			return;

		int index = FindRemoteId(jumperId);
		if (index < 0)
			return;

		if (s_aRemoteVisuals && index < s_aRemoteVisuals.Count())
		{
			IEntity visual = s_aRemoteVisuals[index];
			if (visual)
				SCR_EntityHelper.DeleteEntityAndChildren(visual);
			s_aRemoteVisuals.Remove(index);
		}
		if (s_aRemotePoses && index < s_aRemotePoses.Count())
			s_aRemotePoses.Remove(index);
		s_aRemoteIds.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsFinishedId(RplId jumperId)
	{
		if (!s_aFinishedIds)
			return false;

		int count = s_aFinishedIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			if (s_aFinishedIds[i] == jumperId)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void MarkFinishedId(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return;
		if (IsFinishedId(jumperId))
			return;

		if (!s_aFinishedIds)
			s_aFinishedIds = new array<RplId>();
		s_aFinishedIds.Insert(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	protected static void TickRemoteVisuals()
	{
		if (s_Live)
			return;
		if (!s_aRemoteIds || s_aRemoteIds.IsEmpty())
		{
			if (s_bRemoteTick)
			{
				GetGame().GetCallqueue().Remove(TickRemoteVisuals);
				s_bRemoteTick = false;
			}
			return;
		}

		DriveVisualList(s_aRemoteIds, s_aRemoteVisuals, s_aRemotePoses);
	}

	//------------------------------------------------------------------------------------------------
	protected static void DriveVisualList(array<RplId> ids, array<IEntity> visuals, array<ref MHJ_AiCanopyVisualPose> poses)
	{
		if (!ids)
			return;

		int count = ids.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			IEntity jumper = ResolveJumper(ids[i]);
			if (!jumper)
				continue;

			IEntity visual = null;
			if (visuals && i < visuals.Count())
				visual = visuals[i];
			if (!visual)
			{
				visual = SpawnVisual();
				if (visuals && i < visuals.Count())
					visuals[i] = visual;
			}
			if (!visual)
				continue;

			MHJ_AiCanopyVisualPose pose = EnsurePoseAt(poses, i);
			DriveVisual(visual, jumper, pose);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static MHJ_AiCanopyVisualPose EnsurePoseAt(array<ref MHJ_AiCanopyVisualPose> poses, int index)
	{
		if (!poses)
			return null;

		while (poses.Count() <= index)
		{
			ref MHJ_AiCanopyVisualPose extra = new MHJ_AiCanopyVisualPose();
			poses.Insert(extra);
		}

		MHJ_AiCanopyVisualPose pose = poses[index];
		if (pose)
			return pose;

		ref MHJ_AiCanopyVisualPose created = new MHJ_AiCanopyVisualPose();
		poses[index] = created;
		return created;
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity SpawnVisual()
	{
		Resource resource = Resource.Load(MHJ_Constants.CANOPY_MESH);
		if (!resource || !resource.IsValid())
			return null;

		BaseResourceObject baseResource = resource.GetResource();
		if (!baseResource)
			return null;

		VObject mesh = baseResource.ToVObject();
		if (!mesh)
			return null;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		IEntity visual = GetGame().SpawnEntity(GenericEntity, world, spawnParams);
		if (!visual)
			return null;

		visual.SetObject(mesh, BuildVisibleCanopyRemap(mesh));
		visual.SetFlags(EntityFlags.VISIBLE, true);
		return visual;
	}

	//------------------------------------------------------------------------------------------------
	//! The raw xob only has Chute/Line. 01__Default is a prefab MeshObject slot
	//! on MHJ_DeployedCanopy, not a source material on the mesh. Remapping it
	//! here is `Source material do not exist`.
	protected static string BuildVisibleCanopyRemap(notnull VObject mesh)
	{
		string remap;
		string materials[256];
		int numMats = mesh.GetMaterials(materials);
		int i;
		for (i = 0; i < numMats; i++)
		{
			if (materials[i] == "Chute")
				remap = remap + "$remap 'Chute' '" + MHJ_Constants.CANOPY_MAT_CHUTE + "'; ";
			else if (materials[i] == "Line")
				remap = remap + "$remap 'Line' '" + MHJ_Constants.CANOPY_MAT_LINE + "'; ";
		}
		return remap;
	}

	//------------------------------------------------------------------------------------------------
	//! Local mesh cannot copy the pawn matrix: infantry stay upright and
	//! SetYawPitchRoll on a character does not become canopy pitch/bank.
	//! Authority uses the slot's heading/path/turn; proxies follow motion.
	protected static void DriveVisual(notnull IEntity visual, notnull IEntity jumper, MHJ_AiCanopyVisualPose pose)
	{
		vector origin = jumper.GetOrigin();
		origin[1] = origin[1] + MHJ_Constants.CANOPY_AI_VISUAL_Y;

		float dt = 0.016;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			dt = world.GetTimeSlice();

		float wantYaw;
		float wantPitch;
		float wantBank;
		ResolveVisualAngles(jumper, pose, wantYaw, wantPitch, wantBank);

		vector ypr;
		if (pose)
			pose.Apply(origin, wantYaw, wantPitch, wantBank, dt, ypr);
		else
		{
			ypr[0] = wantYaw;
			ypr[1] = wantPitch;
			ypr[2] = wantBank;
		}

		vector mat[4];
		Math3D.AnglesToMatrix(ypr, mat);
		mat[3] = origin;
		visual.SetWorldTransform(mat);
		visual.Update();

		if (s_bLoggedVisualY)
			return;
		s_bLoggedVisualY = true;
		MHJ_Log.Info("AI canopy visual Y=" + MHJ_Constants.CANOPY_AI_VISUAL_Y.ToString());
	}

	//------------------------------------------------------------------------------------------------
	protected static void ResolveVisualAngles(notnull IEntity jumper, MHJ_AiCanopyVisualPose pose, out float wantYaw, out float wantPitch, out float wantBank)
	{
		wantYaw = jumper.GetYawPitchRoll()[0];
		wantPitch = MHJ_Constants.CANOPY_PITCH_CRUISE;
		wantBank = 0;

		MHJ_AiDropSlot slot = null;
		if (s_Live)
			slot = s_Live.FindSlot(jumper);
		if (slot)
		{
			vector ypr = SlotVisualYpr(slot);
			wantYaw = ypr[0];
			wantPitch = ypr[1];
			wantBank = ypr[2];
			return;
		}

		if (pose && pose.m_bHasRelay)
		{
			wantYaw = pose.m_fRelayYaw;
			wantPitch = pose.m_fRelayPitch;
			wantBank = pose.m_fRelayBank;
			return;
		}

		if (pose && pose.m_bSeeded)
		{
			wantYaw = pose.m_fYaw;
			wantPitch = pose.m_fPitch;
			wantBank = pose.m_fBank;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity ResolveJumper(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return null;

		Managed instance = Replication.FindItem(jumperId);
		if (!instance)
			return null;

		RplComponent replication = RplComponent.Cast(instance);
		if (replication)
			return replication.GetEntity();
		return IEntity.Cast(instance);
	}

	//------------------------------------------------------------------------------------------------
	protected void DestroyVisualAt(int index)
	{
		if (!m_aVisuals || index < 0 || index >= m_aVisuals.Count())
			return;

		IEntity visual = m_aVisuals[index];
		if (visual)
			SCR_EntityHelper.DeleteEntityAndChildren(visual);
		m_aVisuals[index] = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearClientVisuals()
	{
		if (!m_aVisuals)
			return;

		int count = m_aVisuals.Count();
		int i;
		for (i = 0; i < count; i++)
			DestroyVisualAt(i);
		m_aVisuals.Clear();
		if (m_aVisualPoses)
			m_aVisualPoses.Clear();
	}
}
