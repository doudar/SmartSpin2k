#include "PowerTable_Helpers.h"

TestResults PTHelpers::testNeighbors(int i, int j, int testValue) {
  TestResults returnResult;

  // Define direction parameters (start limit, end limit, step, row change, column change)
  const struct {
    int startLimit;
    int endLimit;
    int step;
    int rowChange;
    int colChange;
    TestResults::Neighbor* neighbor;
    bool (*testPredicate)(int16_t, int);
  } directions[] = {// Left: decreasing j, same i
                    {j > 0 ? j - 1 : -1, -1, -1, 0, 0, &returnResult.leftNeighbor, [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }},
                    // Right: increasing j, same i
                    {j < POWERTABLE_WATT_SIZE - 1 ? j + 1 : POWERTABLE_WATT_SIZE, POWERTABLE_WATT_SIZE, 1, 0, 0, &returnResult.rightNeighbor,
                     [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
                    // Top: decreasing i, same j
                    {i > 0 ? i - 1 : -1, -1, -1, 1, 0, &returnResult.topNeighbor, [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
                    // Bottom: increasing i, same j
                    {i < POWERTABLE_CAD_SIZE - 1 ? i + 1 : POWERTABLE_CAD_SIZE, POWERTABLE_CAD_SIZE, 1, 1, 0, &returnResult.bottomNeighbor,
                     [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }}};

  // Process each direction
  for (const auto& dir : directions) {
    // Skip if outside bounds
    if (dir.startLimit == -1 || dir.startLimit == POWERTABLE_WATT_SIZE || dir.startLimit == POWERTABLE_CAD_SIZE) {
      continue;
    }

    // Search for neighbor in this direction
    for (int idx = dir.startLimit; idx != dir.endLimit; idx += dir.step) {
      int row = dir.rowChange ? idx : i;
      int col = dir.rowChange ? j : idx;

      if (this->tableRow[row].tableEntry[col].targetPosition != INT16_MIN) {
        dir.neighbor->targetPosition = this->tableRow[row].tableEntry[col].targetPosition;
        dir.neighbor->i              = row;
        dir.neighbor->j              = col;
        dir.neighbor->found          = 1;
        break;
      }
    }
    // Test if neighbor passes test condition.
    if (dir.testPredicate(dir.neighbor->targetPosition, testValue)) {
      dir.neighbor->passedTest = 1;
    }
  }
  // Check if all neighbors were found.
  if (returnResult.bottomNeighbor.found && returnResult.topNeighbor.found && returnResult.rightNeighbor.found && returnResult.leftNeighbor.found) {
    returnResult.allNeighborsFound = 1;
  }
  // Check if all neighbors passed tests.
  if (returnResult.bottomNeighbor.passedTest && returnResult.topNeighbor.passedTest && returnResult.rightNeighbor.passedTest && returnResult.leftNeighbor.passedTest) {
    returnResult.allNeighborsPassed = 1;
  }

  return returnResult;
}