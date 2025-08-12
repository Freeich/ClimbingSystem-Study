// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ClimbCharacterAnimInstance.generated.h"

class AClimbingSystemCharacter;
class UCustomMovementComponent;
/**
 * 
 */
UCLASS()
class CLIMBINGSYSTEM_API UClimbCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	UPROPERTY()
	AClimbingSystemCharacter* ClimbingSystemCharacter;

	UPROPERTY()
	UCustomMovementComponent* CustomMovementComponent;

	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
	float GroundSpeed;
	void GetGroundSpeed();

	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
	float AirSpeed;
	void GetAirSpeed();

	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
	bool bShouldMove;
	void GetShouldMove();

	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
	bool bIsFalling;
	void GetIsFalling();
	
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
    bool bIsClimbing;
    void GetIsClimbing();

	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = Reference,meta = (AllowPrivateAccess = "true"))
	FVector ClimbVelocity;
	void GetClimbVelocity();

};