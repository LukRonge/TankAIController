# UGV Navigation AI - Observation Design (Option A: Raw + Temporal)

## Overview

This document defines the observation schema for UGV tank drone AI navigation training using UE5 Learning Agents with Imitation Learning.

## Design Principles (Industry Best Practices)

Based on research from Unity ML-Agents, GDC presentations, and UE5 Learning Agents forums:

1. **Minimize Observations**: Include only information the agent genuinely needs
2. **Normalize to [-1, +1] or [0, 1]**: Improves neural network convergence
3. **Use Relative Coordinates**: Position relative to agent, not world space
4. **Avoid Redundancy**: Don't add derived features that duplicate raw data
5. **Let Neural Network Learn**: Raw sensor data often works better than hand-crafted features

## Chosen Approach: Option A (Raw + Temporal)

**Total Observations: 22** (16 traces + 4 navigation + 2 temporal)

This approach:
- Keeps raw line trace distances (16 values)
- Maintains essential navigation observations (waypoint/target direction and distance)
- Adds temporal context (previous actions) to help learn action sequences
- Lets the neural network discover patterns in raw sensor data

## Observation Schema

### 1. Line Traces (16 floats)
Raw distances to obstacles in ellipse pattern around tank.

| Name | Type | Scale | Normalized Range | Description |
|------|------|-------|------------------|-------------|
| LineTraces | Continuous[16] | 1500.0 | [0, 1] | Distance in cm, 0=obstacle, 1=clear |

**Why raw traces**: Neural networks excel at finding patterns in raw sensor data. Pre-processing into sectors removes information the network might use.

### 2. Movement State (1 float)
| Name | Type | Scale | Normalized Range | Description |
|------|------|-------|------------------|-------------|
| ForwardSpeed | Float | 1000.0 | [-1, 1] | Speed in cm/s, negative=reverse |

### 3. Waypoint Navigation (2 values)
| Name | Type | Scale | Normalized Range | Description |
|------|------|-------|------------------|-------------|
| RelativeCurrentWaypointPosition | Direction | N/A | [-1, 1] per axis | LOCAL SPACE direction to waypoint |
| DistanceToCurrentWaypoint | Float | 5000.0 | [0, 1] | Distance in cm |

### 4. Target Navigation (2 values)
| Name | Type | Scale | Normalized Range | Description |
|------|------|-------|------------------|-------------|
| RelativeTargetPosition | Direction | N/A | [-1, 1] per axis | LOCAL SPACE direction to target |
| DistanceToTarget | Float | 10000.0 | [0, 1] | Distance in cm |

### 5. Temporal Context (2 floats) - NEW
| Name | Type | Scale | Normalized Range | Description |
|------|------|-------|------------------|-------------|
| PreviousThrottle | Float | 1.0 | [-1, 1] | Last frame's throttle |
| PreviousSteering | Float | 1.0 | [-1, 1] | Last frame's steering |

**Why temporal context**: Helps model learn action sequences rather than instant reactions. Reduces jerky behavior and enables smooth transitions.

## Action Schema (Unchanged)

| Name | Type | Scale | Range | Description |
|------|------|-------|-------|-------------|
| Throttle | Float | 1.0 | [-1, 1] | Forward/reverse |
| Steering | Float | 1.0 | [-1, 1] | Left/right turn |

## Implementation Changes Required

### BaseTankAIController.h
```cpp
// Add temporal context members
UPROPERTY(BlueprintReadOnly, Category = "Tank|TemporalContext")
float PreviousThrottle = 0.0f;

UPROPERTY(BlueprintReadOnly, Category = "Tank|TemporalContext")
float PreviousSteering = 0.0f;

// Add getters
float GetPreviousThrottle() const { return PreviousThrottle; }
float GetPreviousSteering() const { return PreviousSteering; }
```

### BaseTankAIController.cpp / AILearningAgentsController.cpp
```cpp
// After applying action, store for next frame
void ApplyMovementToTank(float Throttle, float Steering)
{
    // ... existing code ...

    // Store for temporal context (update AFTER applying)
    PreviousThrottle = Throttle;
    PreviousSteering = Steering;
}
```

### TankLearningAgentsInteractor.cpp - SpecifyAgentObservation
```cpp
// Add temporal context observations
ObservationElements.Add(TEXT("PreviousThrottle"),
    ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));

ObservationElements.Add(TEXT("PreviousSteering"),
    ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));
```

### TankLearningAgentsInteractor.cpp - GatherAgentObservation
```cpp
// Get previous actions from controller
const float PrevThrottle = TankController->GetPreviousThrottle();
const float PrevSteering = TankController->GetPreviousSteering();

ObservationElements.Add(TEXT("PreviousThrottle"),
    ULearningAgentsObservations::MakeFloatObservation(InObservationObject, PrevThrottle));

ObservationElements.Add(TEXT("PreviousSteering"),
    ULearningAgentsObservations::MakeFloatObservation(InObservationObject, PrevSteering));
```

## Why NOT VFH/Sector Clearances

Originally considered but rejected:

1. **VFH is for reactive control**: Designed for real-time obstacle avoidance, not imitation learning
2. **Redundant data**: Sector clearances are derived from raw traces - adds no new information
3. **Removes detail**: Min() operation loses information about obstacle shapes
4. **Over-engineering**: Neural networks learn patterns better from raw data
5. **Increases complexity**: More code to maintain, more potential bugs

## Training Recommendations

1. **Input Smoothing**: Already implemented - converts keyboard 0/1/-1 to gradual values
2. **Diverse Scenarios**: Record various situations including tight corners, U-turns
3. **Consistent Speed**: Demonstrate appropriate speeds near obstacles
4. **Recovery Demos**: Show how to back out of stuck situations
5. **Iterations**: Start with 10k, evaluate, increase to 50k if learning curve good

## Expected Benefits

1. **Simpler Code**: Only 2 new observations to add
2. **Faster Training**: Fewer features = faster convergence
3. **Better Generalization**: Raw data lets network find optimal features
4. **Smoother Actions**: Temporal context reduces jerky behavior
5. **Easier Debugging**: Less processing = clearer data flow

## References

- [Unity ML-Agents: Agent Design](https://github.com/Unity-Technologies/ml-agents/blob/main/docs/Learning-Environment-Design-Agents.md)
- [UE5 Learning Agents Forums](https://forums.unrealengine.com/t/learning-agents-driving-tutorial-obstacle-avoidance/2245470)
- [GDC: RL for Racing Games](https://www.gdcvault.com/play/1027669/Reinforcement-Learning-for-Efficient-Cars)
- [MIT: Imitation Learning](https://underactuated.mit.edu/imitation.html)
