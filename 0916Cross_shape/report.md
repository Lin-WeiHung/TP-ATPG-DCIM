# CrossState Cover Solver Report

Generated at: 1758291381

## Fault Summary

Total faults: 34

## Expanded Primitives

Total primitives: 37, total expanded CrossStates: 64

### Fault SA0 / primitive 0 (< 1𝐷/0𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Fault SA1 / primitive 0 (< 0𝐷/1𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Fault TFu / primitive 0 (< 0𝑊1𝐷/0𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Fault TFd / primitive 0 (< 1𝑊0𝐷/1𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Fault CFid(↓,0) / primitive 0 (< 1𝑊0𝐷; 1𝐷/0𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|X|X|X|X|L|
|1|X|X|1|1|1|X|X|X|X|X|R|

### Fault CFid(↓,1) / primitive 0 (< 1𝑊0𝐷; 0𝐷/1𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|0|X|X|X|X|X|X|X|L|
|1|X|X|0|1|1|X|X|X|X|X|R|

### Fault CFid(↑,0) / primitive 0 (< 0𝑊1𝐷; 1𝐷/0𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|X|X|X|X|L|
|1|X|X|1|0|0|X|X|X|X|X|R|

### Fault CFid(↑,1) / primitive 0 (< 0𝑊1𝐷; 0𝐷/1𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|0|X|X|X|X|X|X|X|L|
|1|X|X|0|0|0|X|X|X|X|X|R|

### Fault CIDWD / primitive 0 (< 1𝐶𝑖1𝑊0𝐷/1𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Fault CIDWD / primitive 1 (< 1𝐶𝑖0𝑊1𝐷/0𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|1|1|1|X|single|

### Fault CIDDB(0,0) / primitive 0 (< 0𝐶𝑖; 1𝐷/0𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|0|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|0|Bottom|
|2|X|X|1|X|X|0|X|X|X|0|Both|

### Fault CIDDB(0,1) / primitive 0 (< 0𝐶𝑖; 0𝐷/1𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|0|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|0|Bottom|
|2|X|X|0|X|X|0|X|X|X|0|Both|

### Fault CIDDB(1,0) / primitive 0 (< 1𝐶𝑖; 1𝐷/0𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Fault CIDDB(1,1) / primitive 0 (< 1𝐶𝑖; 0𝐷/1𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|1|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|1|Bottom|
|2|X|X|0|X|X|1|X|X|X|1|Both|

### Fault CIDWDB / primitive 0 (< 1𝐶𝑖; 1𝑊0𝐷/1𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Fault CIDWDB / primitive 1 (< 1𝐶𝑖; 0𝑊1𝐷/0𝐷/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|1|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|1|Bottom|
|2|X|X|0|X|X|1|X|X|X|1|Both|

### Fault CIDD(0,0) / primitive 0 (< 0𝐶𝑖1𝐷/0𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|0|0|0|X|single|

### Fault CIDD(0,1) / primitive 0 (< 0𝐶𝑖0𝐷/1𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|0|0|0|X|single|

### Fault CIDD(1,0) / primitive 0 (< 1𝐶𝑖1𝐷/0𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Fault CIDD(1,1) / primitive 0 (< 1𝐶𝑖0𝐷/1𝐷/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|1|1|1|X|single|

### Fault CDCFst(00,0) / primitive 0 (< 0𝐶𝑖0𝐷; 1𝐷/0𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|0|0|0|X|L|
|1|X|X|1|0|0|X|0|0|0|X|R|

### Fault CDCFst(00,1) / primitive 0 (< 0𝐶𝑖0𝐷; 0𝐷/1𝐷/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|0|X|X|X|0|0|0|X|L|
|1|X|X|0|0|0|X|0|0|0|X|R|

### Fault IC00 / primitive 0 (< 𝐴𝑁𝐷0𝐶𝑖0𝐷/1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Fault IC01 / primitive 0 (< 𝐴𝑁𝐷0𝐶𝑖1𝐷/1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Fault IC10 / primitive 0 (< 𝐴𝑁𝐷1𝐶𝑖0𝐷/1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Fault IC11 / primitive 0 (< 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Fault SDC(11,01) / primitive 0 (< 1𝐶𝑖1𝐷, 𝐴𝑁𝐷0𝐶𝑖1𝐷 /1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Fault SDC(11,10) / primitive 0 (< 1𝐶𝑖1𝐷, 𝐴𝑁𝐷1𝐶𝑖0𝐷 /1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Fault CIDC / primitive 0 (< 0𝐶𝑖/1𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|X|X|X|X|X|X|X|X|single|

### Fault CIDC / primitive 1 (< 1𝐶𝑖/0𝐶𝑜/− >)

CellScope: single  States: 1

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|X|X|X|X|X|X|X|X|single|

### Fault CI(00,11) / primitive 0 (< 0𝐶𝑖0𝐷; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|0|0|0|X|L|
|1|X|X|1|0|0|X|0|0|0|X|R|

### Fault CI(01,11) / primitive 0 (< 0𝐶𝑖1𝐷; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|0|0|0|X|L|
|1|X|X|1|1|1|X|0|0|0|X|R|

### Fault CI(10,11) / primitive 0 (< 1𝐶𝑖0𝐷; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two row-agnostic  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|1|1|1|X|L|
|1|X|X|1|0|0|X|1|1|1|X|R|

### Fault CIDCB(0,00) / primitive 0 (< 0𝐶𝑖; 𝐴𝑁𝐷0𝐶𝑖0𝐷 /1𝐶𝑜/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|0|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|0|Bottom|
|2|X|X|0|X|X|0|X|X|X|0|Both|

### Fault CIDCB(1,11) / primitive 0 (< 1𝐶𝑖; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two cross row  States: 3

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Fault DDCB(0,11) / primitive 0 (< 0𝐷; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two cross row  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|X|X|X|X|Top|
|1|X|X|1|0|0|X|X|X|X|X|Bottom|

### Fault DDCB(1,11) / primitive 0 (< 1𝐷; 𝐴𝑁𝐷1𝐶𝑖1𝐷 /0𝐶𝑜/− >)

CellScope: two cross row  States: 2

|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|X|X|X|X|Top|
|1|X|X|1|1|1|X|X|X|X|X|Bottom|

## Universe (35)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|
|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|0|0|X|1|1|1|X|
|1|0|0|1|X|X|X|1|1|1|X|
|2|X|X|1|1|1|X|0|0|0|X|
|3|1|1|1|X|X|X|0|0|0|X|
|4|X|X|0|0|0|X|0|0|0|X|
|5|0|0|0|X|X|X|0|0|0|X|
|6|X|X|1|0|0|X|0|0|0|X|
|7|0|0|1|X|X|X|0|0|0|X|
|8|X|X|0|X|X|X|0|0|0|X|
|9|X|X|X|X|X|X|X|X|X|X|
|10|X|X|1|X|X|X|0|0|0|X|
|11|X|X|0|X|X|1|X|X|X|1|
|12|X|X|0|X|X|X|X|X|X|1|
|13|X|X|0|X|X|1|X|X|X|X|
|14|X|X|1|X|X|1|X|X|X|1|
|15|X|X|1|X|X|X|X|X|X|1|
|16|X|X|1|X|X|1|X|X|X|X|
|17|X|X|0|X|X|0|X|X|X|0|
|18|X|X|0|X|X|X|X|X|X|0|
|19|X|X|0|X|X|0|X|X|X|X|
|20|X|X|1|X|X|0|X|X|X|0|
|21|X|X|1|X|X|X|X|X|X|0|
|22|X|X|1|X|X|0|X|X|X|X|
|23|X|X|0|X|X|X|1|1|1|X|
|24|X|X|1|X|X|X|1|1|1|X|
|25|X|X|0|0|0|X|X|X|X|X|
|26|0|0|0|X|X|X|X|X|X|X|
|27|X|X|1|0|0|X|X|X|X|X|
|28|0|0|1|X|X|X|X|X|X|X|
|29|X|X|0|1|1|X|X|X|X|X|
|30|1|1|0|X|X|X|X|X|X|X|
|31|X|X|1|1|1|X|X|X|X|X|
|32|1|1|1|X|X|X|X|X|X|X|
|33|X|X|0|X|X|X|X|X|X|X|
|34|X|X|1|X|X|X|X|X|X|X|

## Candidate Sets (37)

### Candidate Set 0 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Candidate Set 1 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Candidate Set 2 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Candidate Set 3 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Candidate Set 4 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|X|X|X|X|L|
|1|X|X|1|1|1|X|X|X|X|X|R|

### Candidate Set 5 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|0|X|X|X|X|X|X|X|L|
|1|X|X|0|1|1|X|X|X|X|X|R|

### Candidate Set 6 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|X|X|X|X|L|
|1|X|X|1|0|0|X|X|X|X|X|R|

### Candidate Set 7 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|0|X|X|X|X|X|X|X|L|
|1|X|X|0|0|0|X|X|X|X|X|R|

### Candidate Set 8 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Candidate Set 9 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|1|1|1|X|single|

### Candidate Set 10 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|0|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|0|Bottom|
|2|X|X|1|X|X|0|X|X|X|0|Both|

### Candidate Set 11 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|0|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|0|Bottom|
|2|X|X|0|X|X|0|X|X|X|0|Both|

### Candidate Set 12 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Candidate Set 13 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|1|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|1|Bottom|
|2|X|X|0|X|X|1|X|X|X|1|Both|

### Candidate Set 14 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Candidate Set 15 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|1|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|1|Bottom|
|2|X|X|0|X|X|1|X|X|X|1|Both|

### Candidate Set 16 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|0|0|0|X|single|

### Candidate Set 17 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|0|0|0|X|single|

### Candidate Set 18 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Candidate Set 19 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|1|1|1|X|single|

### Candidate Set 20 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|0|0|0|X|L|
|1|X|X|1|0|0|X|0|0|0|X|R|

### Candidate Set 21 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|0|X|X|X|0|0|0|X|L|
|1|X|X|0|0|0|X|0|0|0|X|R|

### Candidate Set 22 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Candidate Set 23 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Candidate Set 24 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|X|X|X|X|X|single|

### Candidate Set 25 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|X|X|X|X|single|

### Candidate Set 26 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Candidate Set 27 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|X|1|1|1|X|single|

### Candidate Set 28 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|X|X|X|X|X|X|X|X|single|

### Candidate Set 29 (size=1)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|X|X|X|X|X|X|X|X|single|

### Candidate Set 30 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|0|0|0|X|L|
|1|X|X|1|0|0|X|0|0|0|X|R|

### Candidate Set 31 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|0|0|0|X|L|
|1|X|X|1|1|1|X|0|0|0|X|R|

### Candidate Set 32 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|1|1|1|X|L|
|1|X|X|1|0|0|X|1|1|1|X|R|

### Candidate Set 33 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|0|X|X|0|X|X|X|X|Top|
|1|X|X|0|X|X|X|X|X|X|0|Bottom|
|2|X|X|0|X|X|0|X|X|X|0|Both|

### Candidate Set 34 (size=3)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|X|X|1|X|X|1|X|X|X|X|Top|
|1|X|X|1|X|X|X|X|X|X|1|Bottom|
|2|X|X|1|X|X|1|X|X|X|1|Both|

### Candidate Set 35 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|0|0|1|X|X|X|X|X|X|X|Top|
|1|X|X|1|0|0|X|X|X|X|X|Bottom|

### Candidate Set 36 (size=2)

|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|
|---|---|---|---|---|---|---|---|---|---|---|---|
|0|1|1|1|X|X|X|X|X|X|X|Top|
|1|X|X|1|1|1|X|X|X|X|X|Bottom|

## Solver Result

Chosen set count: 2

- Set 4 covers universe indices: 0,1,2,3,6,7,9,10,14,15,16,20,21,22,24,27,28,31,32,34
- Set 7 covers universe indices: 4,5,8,9,11,12,13,17,18,19,23,25,26,29,30,33
