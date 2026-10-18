# cpp_fixed_point
a bit accurate c++ model of FPGA arthimetic, made to better understand how FPGA arthimetic functions, as well as to verify other projects

## what it does
- `Fixed<W, F>` is a W bit register with F fraction bits (signed or unsigned)
- converts doubles with `round(x * 2^F)`, masks to the width and saturates or wraps on overflow
- rounding modes: floor, toward zero, half up, half even, half away
- add, sub, multiply, divide, and resize between different Q formats
- Q format per signal, `Q<8, 8>` is Q8.8 and `Q<1, 15>` is Q1.15

```cpp
auto a = Q<8, 8>::from_double(1.5);          // raw = 0x0180
auto b = Q<1, 15>::min() * Q<1, 15>::min();  // -1 * -1 saturates to 0.99997
auto c = div<Q<8, 8>>(a, a);                 // 1.0
```

## build
```
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/error_report
```

## results
max error in LSBs vs a double model, 100k random samples each

| format | quantize | add | mul | div |
|---|---|---|---|---|
| Q1.15  | 0.5 | 0 | 0.5 | 0.5 |
| Q8.8   | 0.5 | 0 | 0.5 | 0.5 |
| Q16.16 | 0.5 | 0 | 0.5 | 0.5 |

(this is the error from the op itself, `error_report` also shows the error including input rounding)
