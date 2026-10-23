#include "PhotoModeFilters.h"

#include <initializer_list>
#include <unordered_map>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/window/gui/Gui.h>

#include "port/ShipCompat.h"
#include "port/ui/cvar_prefixes.h"

#include "types.h"
#include "behavior_data.h"
#include "game/memory.h"
#include "game/object_list_processor.h"

#define PHOTO_SHOW_CVAR(name) CVAR_ENHANCEMENT("PhotoMode.Show." name)

namespace {

using Cat = PhotoModeCategory;

const std::vector<PhotoModeCategoryInfo> sCategories = {
    { Cat::Enemies, "Enemies", PHOTO_SHOW_CVAR("Enemies"),
      "Goombas, Bob-ombs, Koopas, Boos, Bullies, Piranha Plants, Thwomps, Amps, etc." },
    { Cat::Bosses, "Bosses", PHOTO_SHOW_CVAR("Bosses"),
      "Bowser, King Bob-omb, King Whomp, Big Boos, Big Bullies, Eyerok, Wiggler." },
    { Cat::Wildlife, "Wildlife", PHOTO_SHOW_CVAR("Wildlife"),
      "Butterflies, birds, fish, Cheep Cheeps, penguins, Hoot, MIPS, Ukiki, Dorrie, manta ray." },
    { Cat::Characters, "Characters", PHOTO_SHOW_CVAR("Characters"),
      "Toad, Bob-omb Buddies, Yoshi, Peach, Lakitu, Mr. Snowman." },
    { Cat::Coins, "Coins", PHOTO_SHOW_CVAR("Coins"), "Yellow, red and blue coins." },
    { Cat::StarsKeys, "Stars & Keys", PHOTO_SHOW_CVAR("StarsKeys"),
      "Power Stars, red coin star markers, Bowser keys." },
    { Cat::PowerUps, "Power-ups & Items", PHOTO_SHOW_CVAR("PowerUps"),
      "Caps, 1-Up mushrooms, recovery hearts, Koopa shells." },
    { Cat::Boxes, "Boxes & Chests", PHOTO_SHOW_CVAR("Boxes"),
      "! boxes, breakable and metal boxes, treasure chests." },
    { Cat::Switches, "Switches & Buttons", PHOTO_SHOW_CVAR("Switches"),
      "Cap switches, purple and blue coin switches, water level diamonds." },
    { Cat::Signs, "Signs & Posts", PHOTO_SHOW_CVAR("Signs"), "Signposts, wall signs, wooden posts, race flags." },
    { Cat::Doors, "Doors, Pipes & Cannons", PHOTO_SHOW_CVAR("Doors"),
      "Doors, star doors, warp pipes, cannons and cannon lids." },
    { Cat::Scenery, "Trees & Scenery", PHOTO_SHOW_CVAR("Scenery"), "Trees, seaweed, castle flags, clock hands." },
    { Cat::Platforms, "Platforms & Machinery", PHOTO_SHOW_CVAR("Platforms"),
      "Moving platforms, elevators, bridges, poles, Clock gears. Hiding these can leave things floating." },
    { Cat::Hazards, "Hazards & Projectiles", PHOTO_SHOW_CVAR("Hazards"),
      "Flames, fire bars, bowling balls, boulders, rolling logs, Bomps, water bombs." },
    { Cat::NaturalEffects, "Natural Effects", PHOTO_SHOW_CVAR("NaturalEffects"),
      "Splashes, bubbles, waves, smoke, dust, leaves, snow, mist." },
    { Cat::GameEffects, "Game Effects", PHOTO_SHOW_CVAR("GameEffects"),
      "Sparkles, star particles, coin numbers and other 'video game' effects." },
    { Cat::Other, "Other / Uncategorized", PHOTO_SHOW_CVAR("Other"), "Anything not in another category." },
};

std::unordered_map<const void*, Cat> sBehaviorCategories;

void Add(Cat cat, std::initializer_list<const BehaviorScript*> behaviors) {
    for (const BehaviorScript* bhv : behaviors) {
        // First assignment wins, so a behavior is never in two categories.
        sBehaviorCategories.emplace(segmented_to_virtual(bhv), cat);
    }
}

void BuildCategoryMap() {
    Add(Cat::Bosses, { bhvKingBobomb, bhvWhompKingBoss, bhvBowser, bhvBowserTailAnchor, bhvBowserBodyAnchor,
                       bhvBowserFlameSpawn, bhvBalconyBigBoo, bhvMerryGoRoundBigBoo, bhvGhostHuntBigBoo,
                       bhvEyerokBoss, bhvEyerokHand, bhvWigglerHead, bhvWigglerBody, bhvBigBully,
                       bhvBigBullyWithMinions, bhvBigChillBully });

    Add(Cat::Enemies,
        { bhvMrI, bhvMrIBody, bhvMrIParticle, bhvChuckya, bhvGrindel, bhvHorizontalGrindel, bhvThwomp, bhvThwomp2,
          bhvSpindrift, bhvHeaveHo, bhvScuttlebug, bhvScuttlebugSpawn, bhvSmallWhomp, bhvBoo, bhvBooWithCage,
          bhvMerryGoRoundBoo, bhvGhostHuntBoo, bhvCourtyardBooTriplet, bhvBooInCastle, bhvBobomb, bhvSmallBully,
          bhvSmallChillBully, bhvMoneybag, bhvMoneybagHidden, bhvPokey, bhvPokeyBodyPart, bhvSwoop, bhvFlyGuy,
          bhvGoomba, bhvGoombaTripletSpawner, bhvChainChomp, bhvChainChompChainPart, bhvEnemyLakitu, bhvCloud,
          bhvCloudPart, bhvSpiny, bhvMontyMole, bhvMontyMoleHole, bhvMrBlizzard, bhvHauntedChair, bhvMadPiano,
          bhvFlyingBookend, bhvBookendSpawn, bhvPiranhaPlant, bhvPiranhaPlantBubble, bhvPiranhaPlantWakingBubbles,
          bhvFirePiranhaPlant, bhvSnufit, bhvKlepto, bhvSkeeter, bhvBubba, bhvUnagi, bhvUnagiSubobject, bhvClamShell,
          bhvSushiShark, bhvSushiSharkCollisionChild, bhvHomingAmp, bhvCirclingAmp, bhvKoopa, bhvFireSpitter,
          bhvBulletBill });

    Add(Cat::Wildlife,
        { bhvButterfly, bhvTripletButterfly, bhvBird, bhvEndBirds1, bhvEndBirds2, bhvFishGroup, bhvFish,
          bhvFishSpawner, bhvManyBlueFishSpawner, bhvFewBlueFishSpawner, bhvBlueFish,
          bhvTankFishGroup, bhvChirpChirp, bhvChirpChirpUnused, bhvBub, bhvHoot, bhvMantaRay,
          bhvDorrie, bhvUkiki, bhvMacroUkiki, bhvMips, bhvPenguinBaby, bhvSmallPenguin, bhvSLWalkingPenguin,
          bhvRacingPenguin, bhvTuxiesMother });

    Add(Cat::Characters, { bhvToadMessage, bhvEndToad, bhvEndPeach, bhvBeginningPeach, bhvBeginningLakitu,
                           bhvCameraLakitu, bhvBobombBuddy, bhvBobombBuddyOpensCannon, bhvYoshi, bhvSnowmansBottom,
                           bhvSnowmansHead, bhvBigSnowmanWhole });

    Add(Cat::Coins, { bhvMrIBlueCoin, bhvCoinInsideBoo, bhvCoinFormationSpawn, bhvCoinFormation, bhvOneCoin,
                      bhvYellowCoin, bhvTemporaryYellowCoin, bhvThreeCoinsSpawn, bhvTenCoinsSpawn,
                      bhvSingleCoinGetsSpawned, bhvMovingYellowCoin, bhvMovingBlueCoin, bhvBlueCoinSliding,
                      bhvBlueCoinJumping, bhvHiddenBlueCoin, bhvRedCoin });

    Add(Cat::StarsKeys, { bhvStar, bhvSpawnedStar, bhvSpawnedStarNoLevelExit, bhvStarSpawnCoordinates,
                          bhvHiddenRedCoinStar, bhvBowserCourseRedCoinStar, bhvHiddenStar, bhvRedCoinStarMarker,
                          bhvUkikiCageStar, bhvCcmTouchedStarSpawn, bhvBowserKey, bhvGrandStar, bhvCelebrationStar,
                          bhvUnlockDoorStar, bhvUnusedFakeStar, bhvBetaBooKey, bhvAlphaBooKey });

    Add(Cat::PowerUps, { bhvWingCap, bhvMetalCap, bhvNormalCap, bhvVanishCap, bhv1upWalking, bhv1upRunningAway,
                         bhv1upSliding, bhv1Up, bhv1upJumpOnApproach, bhvHidden1up, bhvHidden1upInPole,
                         bhvRecoveryHeart, bhvKoopaShell, bhvKoopaShellUnderwater });

    Add(Cat::Boxes, { bhvExclamationBox, bhvRotatingExclamationMark, bhvBreakableBox, bhvBreakableBoxSmall,
                      bhvPushableMetalBox, bhvJumpingBox, bhvJrbSlidingBox, bhvJrbFloatingBox, bhvHiddenObject,
                      bhvTreasureChestsShip, bhvTreasureChestsJrb, bhvTreasureChests, bhvTreasureChestBottom,
                      bhvTreasureChestTop, bhvBetaChestBottom, bhvBetaChestLid });

    Add(Cat::Switches, { bhvCapSwitchBase, bhvCapSwitch, bhvFloorSwitchAnimatesObject, bhvFloorSwitchGrills,
                         bhvFloorSwitchHardcodedModel, bhvFloorSwitchHiddenObjects, bhvPurpleSwitchHiddenBoxes,
                         bhvBlueCoinSwitch, bhvBookSwitch, bhvWaterLevelDiamond });

    Add(Cat::Signs, { bhvMessagePanel, bhvSignOnWall, bhvWoodenPost, bhvKoopaFlag });

    Add(Cat::Doors, { bhvStarDoor, bhvDoor, bhvDoorWarp, bhvWarpPipe, bhvTowerDoor, bhvBowserSubDoor, bhvCannon,
                      bhvCannonClosed, bhvCannonBarrel, bhvCannonBaseUnused, bhvBowserKeyUnlockDoor,
                      bhvBowserKeyCourseExit, bhvOpenableCageDoor, bhvOpenableGrill });

    Add(Cat::Scenery, { bhvTree, bhvSeaweed, bhvSeaweedBundle, bhvCastleFlagWaving, bhvDecorativePendulum,
                        bhvClockMinuteHand, bhvClockHourHand });

    Add(Cat::Hazards,
        { bhvFlame, bhvFlamethrower, bhvFlamethrowerFlame, bhvBouncingFireball, bhvBouncingFireballFlame,
          bhvBowserShockWave, bhvBetaMovingFlames, bhvBetaMovingFlamesSpawn, bhvLllRotatingBlockWithFireBars,
          bhvLllRotatingHexFlame, bhvVolcanoFlames, bhvBlueBowserFlame, bhvFlameFloatingLanding, bhvBlueFlamesGroup,
          bhvFlameBouncing, bhvFlameMovingForwardGrowing, bhvFlameBowser, bhvFlameLargeBurningOut, bhvBowserBomb,
          bhvPitBowlingBall, bhvFreeBowlingBall, bhvBowlingBall, bhvTtmBowlingBallSpawner, bhvBobBowlingBallSpawner,
          bhvThiBowlingBallSpawner, bhvBigBoulder, bhvBigBoulderGenerator, bhvTtmRollingLog, bhvLllRollingLog,
          bhvLllVolcanoFallingTrap, bhvSmallBomp, bhvLargeBomp, bhvWaterBomb, bhvWaterBombSpawner,
          bhvWaterBombShadow, bhvWaterBombCannon, bhvToxBox, bhvBulletBillCannon, bhvSpindel, bhvSmallPiranhaFlame,
          bhvFlyguyFlame, bhvTweester, bhvCoffin, bhvCoffinSpawner, bhvMrBlizzardSnowball, bhvMontyMoleRock,
          bhvSnufitBalls, bhvFallingPillar, bhvFallingPillarHitbox, bhvKoopaShellFlame });

    Add(Cat::Platforms,
        { bhvRotatingPlatform, bhvTower, bhvWfBreakableWallRight, bhvWfBreakableWallLeft, bhvKickableBoard,
          bhvRotatingCounterClockwise, bhvWfRotatingWoodenPlatform, bhvTumblingBridgePlatform, bhvWfTumblingBridge,
          bhvBbhTumblingBridge, bhvLllTumblingBridge, bhvAnotherElavator, bhvRrElevatorPlatform,
          bhvHmcElevatorPlatform, bhvBitfsSinkingPlatforms, bhvBitfsSinkingCagePlatform, bhvDddMovingPole,
          bhvBitfsTiltingInvertedPyramid, bhvSquishablePlatform, bhvRrRotatingBridgePlatform, bhvTowerPlatformGroup,
          bhvWfSlidingTowerPlatform, bhvWfElevatorTowerPlatform, bhvWfSolidTowerPlatform, bhvAnotherTiltingPlatform,
          bhvSquarishPathMoving, bhvTiltingBowserLavaPlatform, bhvFallingBowserPlatform, bhvCheckerboardElevatorGroup,
          bhvCheckerboardPlatformSub, bhvWaterLevelPillar, bhvLllRotatingHexagonalPlatform, bhvLllSinkingRockBlock,
          bhvLllMovingOctagonalMeshPlatform, bhvLllWoodPiece, bhvLllFloatingWoodBridge, bhvLllRotatingHexagonalRing,
          bhvLllSinkingRectangularPlatform, bhvLllSinkingSquarePlatforms, bhvLllTiltingInvertedPyramid,
          bhvLllHexagonalMesh, bhvLllBowserPuzzlePiece, bhvLllBowserPuzzle, bhvLllDrawbridge, bhvWdwExpressElevator,
          bhvWdwExpressElevatorPlatform, bhvBowsersSub, bhvShipPart3, bhvInSunkenShip3, bhvSunkenShipPart,
          bhvSunkenShipPart2, bhvInSunkenShip, bhvInSunkenShip2, bhvHiddenStaircaseStep, bhvBooStaircase,
          bhvBbhTiltingTrapPlatform, bhvHauntedBookshelf, bhvMeshElevator, bhvMerryGoRound, bhvStaticCheckeredPlatform,
          bhvWfSlidingPlatform, bhvSslMovingPyramidWall, bhvPyramidElevator, bhvPyramidElevatorTrajectoryMarkerBall,
          bhvPyramidTop, bhvControllablePlatform, bhvControllablePlatformSub, bhvSlidingSnowMound, bhvSnowMoundSpawn,
          bhvWdwSquareFloatingPlatform, bhvWdwRectangularFloatingPlatform, bhvJrbFloatingPlatform, bhvArrowLift,
          bhvPillarBase, bhvPlatformOnTrack, bhvTrackBall, bhvSeesawPlatform, bhvFerrisWheelAxle,
          bhvFerrisWheelPlatform, bhvTTCRotatingSolid, bhvTTCPendulum, bhvTTCTreadmill, bhvTTCMovingBar, bhvTTCCog,
          bhvTTCPitBlock, bhvTTCElevator, bhvTTC2DRotator, bhvTTCSpinner, bhvSlidingPlatform2,
          bhvOctagonalPlatformRotating, bhvAnimatesOnFloorSwitchPress, bhvActivatedBackAndForthPlatform,
          bhvSwingPlatform, bhvDonutPlatform, bhvDonutPlatformSpawner, bhvRrCruiserWing, bhvGiantPole,
          bhvPoleGrabbing, bhvDDDPole, bhvThiHugeIslandTop, bhvThiTinyIslandTop, bhvMoatGrills, bhvChainChompGate,
          bhvUkikiCage, bhvUkikiCageChild, bhvBooCage });

    Add(Cat::GameEffects,
        { bhvSparkle, bhvSparkleSpawn, bhvSparkleParticleSpawner, bhvCoinSparkles, bhvGoldenCoinSparkles,
          bhvCelebrationStarSparkle, bhvStarKeyCollectionPuffSpawner, bhvWallTinyStarParticle,
          bhvVertStarParticleSpawner, bhvPoundTinyStarParticle, bhvHorStarParticleSpawner, bhvPunchTinyTriangle,
          bhvTriangleParticleSpawner, bhvPurpleParticle, bhvOrangeNumber, bhvJetStreamWaterRing,
          bhvMantaRayWaterRing });

    Add(Cat::NaturalEffects,
        { bhvBubbleParticleSpawner, bhvBubbleMaybe, bhvSmallWaterWave, bhvSmallWaterWave398, bhvWaterAirBubble,
          bhvSmallParticle, bhvPlungeBubble, bhvSmallParticleSnow, bhvSmallParticleBubbles, bhvWhitePuffExplosion,
          bhvWaterMist, bhvBreathParticleSpawner, bhvBreakBoxTriangle, bhvWaterMist2, bhvMistCircParticleSpawner,
          bhvDirtParticleSpawner, bhvSnowParticleSpawner, bhvFireParticleSpawner, bhvBlackSmokeMario,
          bhvBlackSmokeBowser, bhvBlackSmokeUpward, bhvBetaFishSplashSpawner, bhvLeafParticleSpawner, bhvTreeSnow,
          bhvTreeLeaf, bhvWhitePuffSmoke, bhvMistParticleSpawner, bhvWhitePuff1, bhvWhitePuff2, bhvWhitePuffSmoke2,
          bhvTweesterSandParticle, bhvWaterSplash, bhvWaterDroplet, bhvWaterDropletSplash, bhvBubbleSplash,
          bhvIdleWaterWave, bhvObjectWaterSplash, bhvShallowWaterWave, bhvShallowWaterSplash, bhvObjectWaveTrail,
          bhvWaveTrail, bhvTinyStrongWindParticle, bhvStrongWindParticle, bhvObjectBubble, bhvObjectWaterWave,
          bhvExplosion, bhvBobombBullyDeathSmoke, bhvSmoke, bhvBobombExplosionBubble, bhvBobombExplosionBubble3600,
          bhvBobombFuseSmoke, bhvBowserBombExplosion, bhvBowserBombSmoke, bhvCannonBarrelBubbles, bhvSkeeterWave,
          bhvPyramidTopFragment, bhvSLSnowmanWind });
}

Cat CategoryOf(const Object* obj) {
    if (sBehaviorCategories.empty()) {
        BuildCategoryMap();
    }
    auto it = sBehaviorCategories.find(obj->behavior);
    return it != sBehaviorCategories.end() ? it->second : Cat::Other;
}

bool IsPoolObject(const Object* obj) {
    // Some graph nodes (e.g. mirror Mario) are bare GraphNodeObjects without behavior data.
    return obj >= &gObjectPool[0] && obj < &gObjectPool[OBJECT_POOL_CAPACITY];
}

} // namespace

const std::vector<PhotoModeCategoryInfo>& PhotoModeFilters_GetCategories() {
    return sCategories;
}

bool PhotoModeFilters_IsHidden(const Object* obj) {
    if (obj == nullptr || !IsPoolObject(obj) || obj->behavior == nullptr) {
        return false;
    }
    Cat cat = CategoryOf(obj);
    return CVarGetInteger(sCategories[(size_t)cat].cvarName, 1) == 0;
}

void PhotoModeFilters_ApplyPreset(PhotoModeFilterPreset preset) {
    for (const auto& info : sCategories) {
        bool hide = false;
        switch (preset) {
            case PhotoModeFilterPreset::ShowAll:
                break;
            case PhotoModeFilterPreset::NatureDocumentary:
                // Keep the living world and its scenery; drop the "video game" clutter.
                switch (info.category) {
                    case Cat::Coins:
                    case Cat::StarsKeys:
                    case Cat::PowerUps:
                    case Cat::Boxes:
                    case Cat::Switches:
                    case Cat::Signs:
                    case Cat::GameEffects:
                        hide = true;
                        break;
                    default:
                        break;
                }
                break;
            case PhotoModeFilterPreset::CreaturesOnly:
                switch (info.category) {
                    case Cat::Enemies:
                    case Cat::Bosses:
                    case Cat::Wildlife:
                    case Cat::Characters:
                    case Cat::Scenery:
                    case Cat::Platforms:
                    case Cat::NaturalEffects:
                        break;
                    default:
                        hide = true;
                        break;
                }
                break;
            case PhotoModeFilterPreset::EmptyLandscape:
                hide = info.category != Cat::Scenery && info.category != Cat::Platforms &&
                       info.category != Cat::NaturalEffects;
                break;
        }
        CVarSetInteger(info.cvarName, hide ? 0 : 1);
    }
    ShipCompat::GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}
