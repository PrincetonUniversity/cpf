## Steps for constructing a FunctionTree

1. Split basic blocks that belong to a loop if annotations change within
2. Construct root node and add loop container nodes as children to the tree

## What kind of information you can query from the Talkdown module
- `getAnnotationsForInst(Instruction *i)`

  Returns an std::unordered\_map that maps each loop that `i` is part of to a set annotations, e.g.\
  If `i` is inside an inner loop `L1` which is inside the outer loop `L0`, and\
    - there is a `"independent" = "1"` pragma around `L0`, and\
    - there is a `"independent" = "1"` pragma around `L1`, then

  `getAnnotationsForInst(i)` returns something like:\
      \{\
        'L0': \{ \{'independent', '1', 'L0'\} \},\
        'L1': \{ \{'independent', '1', 'L0'\}, \{'independent', '1', 'L1'\} \},\
      \}

## Notes

If annotation belongs to a loop header

## Caveats

If a basic block is not contained within a loop, the annotations are ignored
