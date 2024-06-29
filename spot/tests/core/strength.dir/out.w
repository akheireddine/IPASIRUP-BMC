HOA: v1
States: 1
Start: 0
AP: 1 "a"
acc-name: Buchi
Acceptance: 1 Inf(0)
properties: trans-labels explicit-labels state-acc colored
properties: deterministic weak
--BODY--
State: 0 {0}
[!0] 0
--END--
HOA: v1
States: 3
Start: 0
AP: 3 "b" "a" "c"
acc-name: Buchi
Acceptance: 1 Inf(0)
properties: trans-labels explicit-labels state-acc weak
--BODY--
State: 0
[0&1&!2] 1
[!1&!2] 0
[1&!2] 2
State: 1 {0}
[0] 1
State: 2
[!1&!2] 0
[1&!2] 2
--END--
