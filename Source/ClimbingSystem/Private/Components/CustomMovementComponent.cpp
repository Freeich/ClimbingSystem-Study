// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"

#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/DebugHelper.h"
#include "Components/CapsuleComponent.h"

#pragma region OverridenFunctions
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
	}

	// 从攀爬状态出来
	if(PreviousMovementMode == MOVE_Custom and PreviousCustomMode == ECustomMovementMode::MOVE_Climb)
	{
		bOrientRotationToMovement = true;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f);
		// 这是为了清楚攀爬状态的速度信息。
		StopMovementImmediately();
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
			Debug::Print(TEXT("Can start climbing"));
			StartClimbing();
		}
		else
		{
			Debug::Print(TEXT("Can NOT start climbing"));
		}
	}
	else
	{
		//Stop climbing
		StopClimbing();
	}
}

// 检测是否可以攀爬，返回结果
bool UCustomMovementComponent::CanStartClimbing()
{
	if(IsFalling()) return false;
	if(!TraceClimbableSurfaces()) return false;
	if(!TraceFromEyeHeight(100.f).bBlockingHit) return false;

	return true;
}

void UCustomMovementComponent::StartClimbing()
{
	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_Climb);
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
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);

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

	Debug::Print(TEXT("ClimbableSurfaceLocation: ") + CurrentClimbableSurfaceLocation.ToCompactString(), FColor::Cyan, 1);
	Debug::Print(TEXT("ClimbableSurfaceNormal: ") + CurrentClimbableSurfaceNormal.ToCompactString(), FColor::Red, 2);

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
	const FVector End = Start + UpdatedComponent->GetForwardVector();
	
	ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start,End,true);

	return !ClimbableSurfacesTracedResults.IsEmpty();
}

// 从眼部出发做射线检测，为了检测边缘
FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset)
{
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);
	const FVector Start = ComponentLocation + EyeHeightOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

	return DoLineTraceSingleByObject(Start,End);
}
#pragma endregion 
