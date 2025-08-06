// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"


#pragma region ClimbTraces
void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TraceClimbableSurfaces();
}

TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector& Start, const FVector& End,
                                                                         bool bShowDebugShape)
{
	TArray<FHitResult> OutCapsuleTraceHitResults;

	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		Start,
		End,
		ClimbCapsuleTraceRadius,
		ClimbCapsuleTraceHalfHeight,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		bShowDebugShape ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None,
		OutCapsuleTraceHitResults,
		false
	);
	return OutCapsuleTraceHitResults;
}
#pragma endregion 

#pragma region ClimbCore

// 检测是否可以攀爬，并执行胶囊体检测
void UCustomMovementComponent::TraceClimbableSurfaces()
{
	// 这里使用UpdatedComponent来获取信息
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * 30.f;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset; // 从角色前面一段距离处开始做检测
	const FVector End = Start + UpdatedComponent->GetForwardVector();
	DoCapsuleTraceMultiByObject(Start, End, true);
}
#pragma endregion 
