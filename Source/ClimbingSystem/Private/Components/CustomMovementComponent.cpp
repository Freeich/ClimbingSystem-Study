// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/DebugHelper.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMathLibrary.h"

#pragma region OverridenFunctions

void UCustomMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPlayerAnimInstance = CharacterOwner->GetMesh()->GetAnimInstance();

	if(OwningPlayerAnimInstance)
	{
		// 绑定播放完蒙太奇的回调函数
		OwningPlayerAnimInstance->OnMontageEnded.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
		OwningPlayerAnimInstance->OnMontageBlendingOut.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
	}

	OwningPlayerCharacter = Cast<AClimbingSystemCharacter>(CharacterOwner);
}


void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                             FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// TraceClimbableSurfaces();
	// TraceFromEyeHeight(100.f);
}

// 移动状态发生变化时进行的操作
void UCustomMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// 如果在攀爬状态
	if (IsClimbing())
	{
		bOrientRotationToMovement = false;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(48.f);

		OnEnterClimbStateDelegate.ExecuteIfBound();
	}

	// 从攀爬状态出来
	if(PreviousMovementMode == MOVE_Custom and PreviousCustomMode == ECustomMovementMode::MOVE_Climb)
	{
		bOrientRotationToMovement = true;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f);
		// 这是为了清楚攀爬状态的速度信息。
		const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);// 只保留绕Z轴旋转
		UpdatedComponent->SetWorldRotation(CleanStandRotation);
		
		StopMovementImmediately();

		OnExitClimbStateDelegate.ExecuteIfBound();
	};
	
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

// 重写物理信息客制化函数，会被父类方法调用
void UCustomMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if(IsClimbing())
	{
		PhysClimb(deltaTime, Iterations);
	}
	
	Super::PhysCustom(deltaTime, Iterations);
}

float UCustomMovementComponent::GetMaxSpeed() const
{
	if (IsClimbing())
	{
		return MaxClimbSpeed;
	}
	else
	{
		return Super::GetMaxSpeed();
	}
}

float UCustomMovementComponent::GetMaxAcceleration() const
{
	if (IsClimbing())
	{
		return MaxClimbAcceleration;
	}
	else
	{
		return Super::GetMaxAcceleration();
	}
}

FVector UCustomMovementComponent::ConstrainAnimRootMotionVelocity(const FVector & RootMotionVelocity, const FVector & CurrentVelocity) const
{	
	const bool bIsPlayingRMMontage =
	IsFalling() && OwningPlayerAnimInstance && OwningPlayerAnimInstance->IsAnyMontagePlaying();

	if(bIsPlayingRMMontage)
	{
		return RootMotionVelocity;
	}
	else
	{
		return Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity,CurrentVelocity);
	}
}

#pragma endregion

#pragma region ClimbTraces
// 胶囊体检测
TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector& Start, const FVector& End,
                                                                         bool bShowDebugShape, bool bDrawPersistantShapes)
{
	TArray<FHitResult> OutCapsuleTraceHitResults;

	EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;

	if(bShowDebugShape)
	{
		DebugTraceType = EDrawDebugTrace::ForOneFrame;

		if(bDrawPersistantShapes)
		{
			DebugTraceType = EDrawDebugTrace::Persistent;
		}
	}

	
	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		Start,
		End,
		ClimbCapsuleTraceRadius,
		ClimbCapsuleTraceHalfHeight,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutCapsuleTraceHitResults,
		false
	);
	return OutCapsuleTraceHitResults;
}

// 射线检测
FHitResult UCustomMovementComponent::DoLineTraceSingleByObject(const FVector& Start, const FVector& End,
	bool bShowDebugShape, bool bDrawPersistantShapes)
{
	FHitResult OutHit;

	EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;

	if(bShowDebugShape)
	{
		DebugTraceType = EDrawDebugTrace::ForOneFrame;

		if(bDrawPersistantShapes)
		{
			DebugTraceType = EDrawDebugTrace::Persistent;
		}
	}
	
	UKismetSystemLibrary::LineTraceSingleForObjects(
		this,
		Start,
		End,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutHit,
		false
	);
	return OutHit;
}
#pragma endregion 


#pragma region ClimbCore
// 执行切换攀爬状态的操作
void UCustomMovementComponent::ToggleClimbing(bool bEnableClimb)
{
	if(bEnableClimb)
	{
		if(CanStartClimbing())
		{
			//Enter the climb state
			// Debug::Print(TEXT("Can start climbing"));
			StartClimbing();
			
			// 我没有这个进入动画，所以不进行这一步
			PlayClimbMontage(IdleToClimbMontage);
		}
		else if(CanClimbDownLedge())
		{
			
			PlayClimbMontage(ClimbDownLedgeMontage);
		}
		else
		{
			TryVaultingOrMantling();
		}
	}
	if(not bEnableClimb)
	{
		//Stop climbing
		StopClimbing();
	}
}

// 检测是否可以攀爬，返回结果
bool UCustomMovementComponent::CanStartClimbing()
{
	// if(IsFalling()) return false;
	if(!TraceClimbableSurfaces()) return false;
	if(!TraceFromEyeHeight(100.f).bBlockingHit) return false;

	return true;
}

bool UCustomMovementComponent::CanClimbDownLedge()
{
	if(IsFalling()) return false;

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	const FVector WalkableSurfaceTraceStart = ComponentLocation + ComponentForward * ClimbDownWalkableSurfaceTraceOffset;
	const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

	// FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd,true, true);
	FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

	const FVector LedgeTraceStart = WalkableSurfaceHit.TraceStart + ComponentForward * ClimbDownLedgeTraceOffset;
	const FVector LedgeTraceEnd = LedgeTraceStart + DownVector * 200.f;

	// FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd,true, true);
	FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd);

	if(WalkableSurfaceHit.bBlockingHit && !LedgeTraceHit.bBlockingHit)
	{
		return true;
	}

	return false;
}

bool UCustomMovementComponent::TryClimbDownLedge()
{
	if(IsFalling()) return false;

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	const FVector WalkableSurfaceTraceStart = ComponentLocation + ComponentForward * ClimbDownWalkableSurfaceTraceOffset;
	const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

	// FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd,true, true);
	FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

	const FVector LedgeTraceStart = WalkableSurfaceHit.TraceStart + ComponentForward * ClimbDownLedgeTraceOffset;
	const FVector LedgeTraceEnd = LedgeTraceStart + DownVector * 200.f;

	// FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd,true, true);
	FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd);

	if(WalkableSurfaceHit.bBlockingHit && !LedgeTraceHit.bBlockingHit)
	{
		return true;
	}

	return false;	
}


void UCustomMovementComponent::StartClimbing()
{
	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_Climb);
	StopMovementImmediately();
}

void UCustomMovementComponent::StopClimbing()
{
	SetMovementMode(MOVE_Falling);
}

// 设置攀爬的各种物理信息
// 这个方法会每帧执行，因为调用这个方法的方法会每帧执行，也就是PhysCustom会每帧执行
// 一旦进入了攀爬状态，那么这个方法就会每帧都执行了
void UCustomMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// 处理所有的表面信息
	TraceClimbableSurfaces();
	ProcessClimableSurfaceInfo();
	
	// 判断是都应该停止攀爬了
	if(CheckShouldStopClimbing() || CheckHasReachedFloor())
	{
		StopClimbing();
	}
	
	RestorePreAdditiveRootMotionVelocity();

	if( !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{
		// 这里需要定义最大攀爬速度 和 加速度
		CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);

	// 处理攀爬的旋转
	SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		//adjust and try again
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	// 把移动Snap到可以攀爬的表面
	SnapMovementToClimableSurfaces(deltaTime);

	if(CheckHasReachedLedge())
	{
		// CharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlayClimbMontage(ClimbToTopMontage);
	}
	
}

// 这里做的操作就是，把所有碰撞到的点做平均值处理
void UCustomMovementComponent::ProcessClimableSurfaceInfo()
{
	CurrentClimbableSurfaceLocation = FVector::ZeroVector;
	CurrentClimbableSurfaceNormal = FVector::ZeroVector;

	if(ClimbableSurfacesTracedResults.IsEmpty()) return;

	for (const FHitResult& TraceHitResult : ClimbableSurfacesTracedResults)
	{
		CurrentClimbableSurfaceLocation += TraceHitResult.ImpactPoint;
		CurrentClimbableSurfaceNormal += TraceHitResult.ImpactNormal;
	}

	CurrentClimbableSurfaceLocation /= ClimbableSurfacesTracedResults.Num();
	CurrentClimbableSurfaceNormal = CurrentClimbableSurfaceNormal.GetSafeNormal();
}

bool UCustomMovementComponent::CheckShouldStopClimbing()
{
	// 胶囊体探测没有检测到结果了，也停止攀爬
	if (ClimbableSurfacesTracedResults.IsEmpty()) return true;

	// 计算攀爬到边缘停止攀爬的条件
	// 墙面法向量 和 世界向上的法向量 做点积
	const float DotResult = FVector::DotProduct(CurrentClimbableSurfaceNormal, FVector::UpVector);
	const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));
	
	// 小于六十度就停止攀爬
	if(DegreeDiff < 60.f) return true;

	// Debug::Print(TEXT("Degree Diff: ") + FString::SanitizeFloat(DegreeDiff), FColor::Cyan, 1);
	return false;
}

// 检测是否到达了地板，向下做一个胶囊体检测
bool UCustomMovementComponent::CheckHasReachedFloor()
{
	const FVector DownVector = -UpdatedComponent->GetUpVector();
	const FVector StartOffset = DownVector * 50.f;

	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + DownVector;

	TArray<FHitResult> PossibleFloorHits = DoCapsuleTraceMultiByObject(Start,End);

	if(PossibleFloorHits.IsEmpty()) return false;

	for(const FHitResult& PossibleFloorHit:PossibleFloorHits)
	{
		// 这是为了避免在地面刚一进攀爬状态就退出来
		// 所以只有在接触到了地面，同时还向下爬的时候才退出来
		const bool bFloorReached =
		FVector::Parallel(-PossibleFloorHit.ImpactNormal,FVector::UpVector) && // 如果检测到地面的法向量 和 世界向上的法向量平行
		GetUnrotatedClimbVelocity().Z<-10.f;

		if(bFloorReached)
		{
			return true;
		}
	}

	return false;
}

FQuat UCustomMovementComponent::GetClimbRotation(float DeltaTime)
{
	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();

	// 如果现在是根运动支配那么不改变旋转；
	if(HasAnimRootMotion() or CurrentRootMotion.HasOverrideVelocity())
	{
		return CurrentQuat;
	}

	// 如果不是就创建一个让本地X轴转向墙的法向量的反方向的旋转。
	const FQuat TargetQuat = FRotationMatrix::MakeFromX(-CurrentClimbableSurfaceNormal).ToQuat();
	
	return FMath::QInterpTo(CurrentQuat, TargetQuat, DeltaTime, 5.f);
}

void UCustomMovementComponent::SnapMovementToClimableSurfaces(float DeltaTime)
{
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

	const FVector ProjectedCharacterToSurface = (CurrentClimbableSurfaceLocation - ComponentLocation).ProjectOnTo(ComponentForward);

	const FVector SnapVector = -CurrentClimbableSurfaceNormal * ProjectedCharacterToSurface.Length() + CurrentClimbableSurfaceNormal * 50.f;
	// 这里是真正操作让角色贴着墙
	UpdatedComponent->MoveComponent(
		SnapVector * DeltaTime * MaxClimbSpeed,
		UpdatedComponent->GetComponentQuat(),
		true
	);
}

// 判断是否到达上边界
bool UCustomMovementComponent::CheckHasReachedLedge()
{	
	FHitResult LedgetHitResult = TraceFromEyeHeight(100.f,0.f);

	if(!LedgetHitResult.bBlockingHit)
	{
		const FVector WalkableSurfaceTraceStart = LedgetHitResult.TraceEnd;

		const FVector DownVector = -UpdatedComponent->GetUpVector();
		const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;
		const float DotResult = FVector::DotProduct(-1 * UpdatedComponent->GetForwardVector(), FVector::UpVector);
		const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));

		FHitResult WalkabkeSurfaceHitResult =
		// DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd,true, true);
		DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

		// 当前处于合适角度，且上方有平台时，可以播放登顶蒙太奇
		if(60.f <= DegreeDiff and DegreeDiff <= 90.f and WalkabkeSurfaceHitResult.bBlockingHit && GetUnrotatedClimbVelocity().Z > 10.f)
		{
			return true;
		}
	}

	return false;
}

void UCustomMovementComponent::TryVaultingOrMantling()
{	
	FVector StartPosition;
	FVector LandPosition;

	int MontageIndex = CanStartVaultingOrMantling(StartPosition,LandPosition);

	if(MontageIndex == -1) return; // 等于-1说明没碰到 不执行
	
	if(MontageIndex == 0) // vaulting
	{
		//Start vaulting
		SetMotionWarpTarget(FName("VaultStartPoint"),StartPosition);
		SetMotionWarpTarget(FName("VaultEndPoint"),LandPosition);

		// StartClimbing();
		// 这里需要设置为飞翔模式，不然重力不会被忽略
		SetMovementMode(MOVE_Flying);
		// 还得把胶囊体碰撞关了，不然会被胶囊体卡住过不去
		CharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlayClimbMontage(VaultMontage);
	}
	else if (MontageIndex == 1) // mantle low
	{
		//Start Mantling
		SetMotionWarpTarget(FName("MantleStartPoint"),StartPosition);
		SetMotionWarpTarget(FName("MantleLandPoint"),LandPosition);
		

		// StartClimbing();
		// 这里需要设置为飞翔模式，不然重力不会被忽略
		SetMovementMode(MOVE_Flying);
		// 还得把胶囊体碰撞关了，不然会被胶囊体卡住过不去
		CharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlayClimbMontage(MantleMontage);
	}
}

int UCustomMovementComponent::CanStartVaultingOrMantling(FVector& OutStartPosition,FVector& OutLandPosition)
{
	if(IsFalling()) return false;

	OutStartPosition = FVector::ZeroVector;
	OutLandPosition = FVector::ZeroVector;

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector UpVector = UpdatedComponent->GetUpVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	FVector LandBoundary = FVector::ZeroVector;
	
	for(int32 i = 0; i<5; i++)
	{
		const FVector Start = ComponentLocation + UpVector * 100.f + 
		ComponentForward * 50.f * (i+1);

		const FVector End = Start + DownVector * 120.f * (i+1);

		// FHitResult VaultTraceHit = DoLineTraceSingleByObject(Start,End,true, true);
		FHitResult VaultTraceHit = DoLineTraceSingleByObject(Start,End);

		if(i == 0 && VaultTraceHit.bBlockingHit)
		{
			OutStartPosition = VaultTraceHit.ImpactPoint - FVector(0.f, 0.f, 80.f) - ComponentForward * 60.f;
		}

		if(i == 1 and VaultTraceHit.bBlockingHit)
		{
			LandBoundary = VaultTraceHit.ImpactPoint;
		}
		
		if(i == 3 && VaultTraceHit.bBlockingHit)
		{
			OutLandPosition = VaultTraceHit.ImpactPoint;
		}
	}

	if(OutStartPosition!=FVector::ZeroVector && OutLandPosition!=FVector::ZeroVector)
	{
		float HeightBoundary = CharacterOwner->BaseEyeHeight * 0.6;
		// if (OutVaultStartPosition.Z < HeightBoundary) // 开始点小于眼部高度的0.6，判断是翻越还是爬上，根据墙的后续高度
		// {
		if(LandBoundary.Z < OutStartPosition.Z * 0.3f) // 第二个碰撞点的高度太低，说明可以翻越
		{
			return 0;
		}
		else
		{
			OutStartPosition += FVector(0.f, 0.f, 80.f);
			OutLandPosition = OutStartPosition + ComponentForward * 50.f;
			return 1;	// 否则爬上
		}
		// }
	}

	return -1;
}



bool UCustomMovementComponent::IsClimbing() const
{	
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
}


// 检测是否可以攀爬，并执行胶囊体检测
bool UCustomMovementComponent::TraceClimbableSurfaces()
{
	// 这里使用UpdatedComponent来获取信息
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * 30.f;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset; // 从角色前面一段距离处开始做检测
	

	// const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	// const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * CharacterOwner->BaseEyeHeight;
	// const FVector Start = ComponentLocation + EyeHeightOffset;


	const FVector End = Start + UpdatedComponent->GetForwardVector();
	
	// ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start,End,true, true);
	ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start,End);

	return !ClimbableSurfacesTracedResults.IsEmpty();
}

// 从眼部出发做射线检测，为了检测边缘
// TraceDistance 是 射线长度
// TraceStartOffset 是 从眼部向头顶上(组件坐标系)的偏移
FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset, bool bShowDebugShape, bool bDrawPersistantShapes)
{
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);
	const FVector Start = ComponentLocation + EyeHeightOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

	return DoLineTraceSingleByObject(Start,End,bShowDebugShape,bDrawPersistantShapes);
}


void UCustomMovementComponent::PlayClimbMontage(UAnimMontage * MontageToPlay)
{
	if(!MontageToPlay) return;
	if(!OwningPlayerAnimInstance) return;
	if(OwningPlayerAnimInstance->IsAnyMontagePlaying()) return;

	OwningPlayerAnimInstance->Montage_Play(MontageToPlay);
}

void UCustomMovementComponent::OnClimbMontageEnded(UAnimMontage * Montage, bool bInterrupted)
{
	if(Montage == IdleToClimbMontage or Montage == ClimbDownLedgeMontage)
	{
		StartClimbing();
	}
	
	if(Montage == ClimbToTopMontage or Montage == VaultMontage or Montage == MantleMontage)
	{
		SetMovementMode(MOVE_Walking);
		CharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}


void UCustomMovementComponent::RequestHopping()
{	
	const FVector UnrotatedLastInputVector =
	UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(),GetLastInputVector());
	
	const float DotResult =
	FVector::DotProduct(UnrotatedLastInputVector.GetSafeNormal(), FVector::UpVector);

	if(DotResult>=0.9f)
	{
		HandleHopUp();
	}
	else if(DotResult<=-0.9f)
	{
		HandleHopDown();
	}
}


void UCustomMovementComponent::SetMotionWarpTarget(const FName & InWarpTargetName, const FVector & InTargetPosition)
{
	if(!OwningPlayerCharacter) return;

	OwningPlayerCharacter->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromLocation(
		InWarpTargetName,
		InTargetPosition
	);
}


void UCustomMovementComponent::HandleHopUp()
{	
	FVector HopUpTargetPoint;

	if(CheckCanHopUp(HopUpTargetPoint))
	{
		SetMotionWarpTarget(FName("HopUpTargetPoint"),HopUpTargetPoint);

		PlayClimbMontage(HopUpMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopUp(FVector& OutHopUpTargetPosition)
{	
	FHitResult HopUpHit = TraceFromEyeHeight(100.f,-20.f,true,true);
	FHitResult SaftyLedgeHit = TraceFromEyeHeight(100.f,150.f,true,true);

	if(HopUpHit.bBlockingHit && SaftyLedgeHit.bBlockingHit)
	{	
		OutHopUpTargetPosition = HopUpHit.ImpactPoint;

		return true;
	}

	return false;
}

void UCustomMovementComponent::HandleHopDown()
{
	FVector HopDownTargetPoint;

	if(CheckCanHopDown(HopDownTargetPoint))
	{
		SetMotionWarpTarget(FName("HopDownTargetPoint"),HopDownTargetPoint);

		PlayClimbMontage(HopDownMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopDown(FVector & OutHopDownTargetPosition)
{
	FHitResult HopDownHit = TraceFromEyeHeight(100.f,-300.f);

	if(HopDownHit.bBlockingHit)
	{
		OutHopDownTargetPosition = HopDownHit.ImpactPoint;

		return true;
	}

	return false;
}


FVector UCustomMovementComponent::GetUnrotatedClimbVelocity() const
{
	// 这里获得的是角色速度在当前Component坐标系下的速度，并不是世界速度
	return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}

#pragma endregion 
