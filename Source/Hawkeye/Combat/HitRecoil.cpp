#include "Combat/HitRecoil.h"

#include "Combat/MeleeRules.h"

namespace
{
	/** The angular frequency and damping ratio the closed form uses, both kept in range. */
	void MakeSpring(float Hz, float Damping, float& OutOmega, float& OutZeta)
	{
		OutOmega = 2.f * PI * FMath::Max(Hz, 0.01f);
		OutZeta = FMath::Clamp(Damping, 0.01f, 0.99f);
	}

	/** One axis, exactly: x(t) = e^(-a t) (A cos wd t + B sin wd t) with A = x0 and B = (v0 + a x0) / wd. */
	void StepAxis(double& X, double& V, double Seconds, double Omega, double Zeta)
	{
		const double A = Zeta * Omega;
		const double Wd = Omega * FMath::Sqrt(1.0 - Zeta * Zeta);
		const double E = FMath::Exp(-A * Seconds);
		const double C = FMath::Cos(Wd * Seconds);
		const double S = FMath::Sin(Wd * Seconds);
		const double Coef = (V + A * X) / Wd;
		const double NewX = E * (X * C + Coef * S);
		const double NewV = -A * NewX + E * Wd * (-X * S + Coef * C);
		X = NewX;
		V = NewV;
	}
}

float FHawkeyeRecoilSpring::PeakPerRate(float Hz, float Damping)
{
	float Omega = 0.f;
	float Zeta = 0.f;
	MakeSpring(Hz, Damping, Omega, Zeta);
	// x(t) = (v0 / wd) e^(-zeta w t) sin(wd t); it peaks where tan(wd t) = wd / (zeta w), where sin = sqrt(1 - zeta^2).
	const float Root = FMath::Sqrt(1.f - Zeta * Zeta);
	const float Decay = FMath::Exp(-Zeta / Root * FMath::Atan2(Root, Zeta));
	return Decay / Omega;
}

void FHawkeyeRecoilSpring::Kick(const FVector& Direction, float PeakDegrees, float Hz, float Damping)
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero() || PeakDegrees <= 0.f)
	{
		return;
	}
	const float Speed = PeakDegrees / PeakPerRate(Hz, Damping);
	Rate += FVector2D(Flat.X, Flat.Y) * Speed;
}

void FHawkeyeRecoilSpring::Advance(float Seconds, float Hz, float Damping)
{
	if (Seconds <= 0.f)
	{
		return;
	}
	float Omega = 0.f;
	float Zeta = 0.f;
	MakeSpring(Hz, Damping, Omega, Zeta);
	double X = Offset.X;
	double Y = Offset.Y;
	double VX = Rate.X;
	double VY = Rate.Y;
	StepAxis(X, VX, Seconds, Omega, Zeta);
	StepAxis(Y, VY, Seconds, Omega, Zeta);
	Offset = FVector2D(X, Y);
	Rate = FVector2D(VX, VY);
}

bool FHawkeyeRecoilSpring::IsAtRest(float Degrees) const
{
	return Offset.Size() <= Degrees && Rate.Size() <= Degrees * 4.f;
}

void FHawkeyeHitRecoil::Kick(const FVector& Away, bool bHeavy, const FHawkeyeHitRecoilTuning& Tuning, float Scale)
{
	const float Factor = FMath::Max(Scale, 0.f);
	Spine.Kick(Away, (bHeavy ? Tuning.HeavySpineDegrees : Tuning.LightSpineDegrees) * Factor, Tuning.SpineHz, Tuning.SpineDamping);
	Head.Kick(Away, (bHeavy ? Tuning.HeavyHeadDegrees : Tuning.LightHeadDegrees) * Factor, Tuning.HeadHz, Tuning.HeadDamping);
}

void FHawkeyeHitRecoil::Advance(float Seconds, const FHawkeyeHitRecoilTuning& Tuning)
{
	Spine.Advance(Seconds, Tuning.SpineHz, Tuning.SpineDamping);
	Head.Advance(Seconds, Tuning.HeadHz, Tuning.HeadDamping);
	if (Spine.IsAtRest(Tuning.RestDegrees))
	{
		Spine.Reset();
	}
	if (Head.IsAtRest(Tuning.RestDegrees))
	{
		Head.Reset();
	}
}

bool FHawkeyeHitRecoil::IsActive(const FHawkeyeHitRecoilTuning& Tuning) const
{
	return !Spine.IsAtRest(Tuning.RestDegrees) || !Head.IsAtRest(Tuning.RestDegrees);
}

FRotator FHawkeyeHitRecoil::MakeTipRotation(const FVector& OffsetDegrees)
{
	const FVector Flat(OffsetDegrees.X, OffsetDegrees.Y, 0.f);
	const float Degrees = static_cast<float>(Flat.Size());
	if (Degrees <= KINDA_SMALL_NUMBER)
	{
		return FRotator::ZeroRotator;
	}
	return UHawkeyeMeleeRules::ComputeHitLeanRotation(Flat / Degrees, 1.f, Degrees);
}

FRotator FHawkeyeHitRecoil::MakeHeadRotation(const FVector& OffsetDegrees, const FVector& Right, float YawShare)
{
	FRotator Rotation = MakeTipRotation(OffsetDegrees);
	const FVector Side = Right.GetSafeNormal2D();
	if (!Side.IsNearlyZero())
	{
		const FVector Flat(OffsetDegrees.X, OffsetDegrees.Y, 0.f);
		Rotation = (FRotator(0.f, YawShare * (Flat | Side), 0.f).Quaternion() * Rotation.Quaternion()).Rotator();
	}
	return Rotation;
}
