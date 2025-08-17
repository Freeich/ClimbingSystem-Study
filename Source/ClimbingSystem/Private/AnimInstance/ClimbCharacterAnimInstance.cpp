// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/ClimbCharacterAnimInstance.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

#include "ClimbingSystem/DebugHelper.h"

void UClimbCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	ClimbingSystemCharacter = Cast<AClimbingSystemCharacter>(TryGetPawnOwner());

	if(ClimbingSystemCharacter)
	{
		CustomMovementComponent = ClimbingSystemCharacter->GetCustomMovementComponent();
	}
}

void UClimbCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if(!ClimbingSystemCharacter || !CustomMovementComponent) return;

	GetGroundSpeed();
	GetAirSpeed();
	GetShouldMove();
	GetIsFalling();
	GetIsClimbing();
	GetClimbVelocity();
	GetClimbDirection();
}

void UClimbCharacterAnimInstance::GetGroundSpeed()
{	
	GroundSpeed = UKismetMathLibrary::VSizeXY(ClimbingSystemCharacter->GetVelocity());
}

void UClimbCharacterAnimInstance::GetAirSpeed()
{
	AirSpeed = ClimbingSystemCharacter->GetVelocity().Z;
}

void UClimbCharacterAnimInstance::GetShouldMove()
{	
	bShouldMove =
	CustomMovementComponent->GetCurrentAcceleration().Size()>0&&
	GroundSpeed>5.f &&
	!bIsFalling;
}

void UClimbCharacterAnimInstance::GetIsFalling()
{
	bIsFalling = CustomMovementComponent->IsFalling();
}

void UClimbCharacterAnimInstance::GetIsClimbing()
{
	bIsClimbing = CustomMovementComponent->IsClimbing();
}

void UClimbCharacterAnimInstance::GetClimbVelocity()
{
	ClimbVelocity = CustomMovementComponent->GetUnrotatedClimbVelocity();
}

void UClimbCharacterAnimInstance::GetClimbDirection()
{

	// 以下都是组件坐标系下的向量表示
	FVector ComponentUpVector = UKismetMathLibrary::Quat_UnrotateVector(CustomMovementComponent->UpdatedComponent->GetComponentQuat(), CustomMovementComponent->UpdatedComponent->GetUpVector());
	FVector NormalizedVelocity = ClimbVelocity.GetSafeNormal();
	if (NormalizedVelocity == FVector::ZeroVector)
	{
		ClimbDirection = EClimbDirection::Up;
	}
	else
	{
		// 点积算角度
		float DotResult = FVector::DotProduct(ComponentUpVector, NormalizedVelocity);
		float Degree = FMath::RadiansToDegrees(acos(DotResult));

		// 叉积算左右
		FVector CrossResult = FVector::CrossProduct(ComponentUpVector, NormalizedVelocity);
		int direction = -1 * CrossResult.X > 0 ?  1 : -1;
		float DirectedDegree = Degree * direction;
	
		if(DirectedDegree < 20.f and DirectedDegree > -20.f)
		{
			ClimbDirection = EClimbDirection::Up;
		}
		else if (DirectedDegree <= -20.f and DirectedDegree >= -70.f)
		{
			ClimbDirection = EClimbDirection::LeftUp;
		}
		else if (DirectedDegree < -70.f and DirectedDegree > -110.f)
		{
			ClimbDirection = EClimbDirection::Left;
		}
		else if (DirectedDegree <= -110.f and DirectedDegree >= -160.f)
		{
			ClimbDirection = EClimbDirection::LeftDown;
		}
		else if (DirectedDegree < -160.f or DirectedDegree > 160.f)
		{
			ClimbDirection = EClimbDirection::Down;
		}
		else if (DirectedDegree <= 160.f and DirectedDegree >= 110.f)
		{
			ClimbDirection = EClimbDirection::RightDown;
		}
		else if (DirectedDegree < 110.f and DirectedDegree > 70.f)
		{
			ClimbDirection = EClimbDirection::Right;
		}
		else if (DirectedDegree <= 70.f and DirectedDegree >= 20.f)
		{
			ClimbDirection = EClimbDirection::RightUp;
		}
		// Debug::Print(TEXT("攀爬角度: ") + FString::SanitizeFloat(DirectedDegree), FColor::Cyan, 5);
	}
}
