# OSY Lab 1-1

## Program `add` / `inv`

V jazyce **C** vytvořte program, který se bude chovat různě podle názvu, pod kterým byl spuštěn.

Program musí podporovat dvě varianty:

* `add` – upraví vstupní hodnoty jejich **zvýšením o 1**,
* `inv` – provede **inverzi vstupních hodnot**.

Program nesmí mít dvě samostatné implementace. Obě varianty musí být realizovány v jednom programu a chování programu se určí podle názvu, pod kterým byl spuštěn.

## Spuštění programu

Předpokládejme, že výsledný program se jmenuje `transform`.

Vytvořte dvě symbolické odkazy:

```bash
ln -s transform add
ln -s transform inv
```

Program tedy může být spuštěn například jako:

```bash
./add
```

nebo

```bash
./inv
```

Název, pod kterým byl program spuštěn, zjistěte pomocí argumentů předaných programu při jeho spuštění.

---

## Vstup

Program čte ze **standardního vstupu (`stdin`) posloupnost hodnot oddělených bílými znaky**.

Vstup může obsahovat:

* pouze celá čísla, např.

```text
1 2 3 4
```

* nebo pouze jednotlivá písmena anglické abecedy, např.

```text
a B c D
```

Vstup **nesmí kombinovat čísla a písmena**.

Počet vstupních hodnot není předem znám.

---

## Varianta `add`

Pokud je program spuštěn pod názvem `add`, každou vstupní hodnotu upraví podle následujících pravidel:

### Čísla

Každé číslo se zvětší o `1`.

```text
1 2 3 4  →  2 3 4 5
```

### Písmena

Každé písmeno se posune o jednu pozici v anglické abecedě.

```text
a b c  →  b c d 
```

Velikost písmene musí být zachována:

```text
a B c D  →  b C d E 
```

Pro účely úlohy uvažujte abecedu **cyklicky**, tedy:

```text
z → a
Z → A
```

---

## Varianta `inv`

Pokud je program spuštěn pod názvem `inv`, každou vstupní hodnotu upraví podle následujících pravidel:

### Čísla

Změní znaménko čísla:

```text
1 2 -3 4  →  -1 -2 3 -4 
```

### Písmena

Změní velikost písmene:

```text
a B c D  →  A b C d
```

---

## Neplatný vstup

Pokud vstup obsahuje současně **čísla i písmena**, program:

1. vypíše chybové hlášení na **standardní chybový výstup (`stderr`)**,
2. na `stdout` nevypíše žádný výsledek,
3. skončí s nenulovým návratovým kódem.

Například:

```text
1 2 a b
```

má být považováno za chybu. Chybové hlášení může mít například podobu:

```text
Error: mixed input types
```

---

## Neplatný název programu

Pokud je program spuštěn pod jiným názvem než `add` nebo `inv`, program:

* vypíše chybové hlášení na `stderr`,
* skončí s nenulovým návratovým kódem.

Například:

```bash
./transform
```

má skončit chybou.

---

## Příklad použití

```bash
$ echo "1 2 3" | ./add
2 3 4

$ echo "a b c" | ./add
b c d

$ echo "1 2 3" | ./inv
-1 -2 -3

$ echo "a B C" | ./inv
A b c
```

Chybový vstup:

```bash
$ echo "1 2 a b" | ./add
Error: mixed input types
```

Chybový výstup musí být skutečně zapisován na `stderr`, nikoli na `stdout`.

Program by měl být schopen zpracovat **libovolně dlouhý vstup**, aniž by předem předpokládal jeho maximální počet položek.