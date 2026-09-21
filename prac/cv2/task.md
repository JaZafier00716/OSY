# OSY Lab 1-2
 
## Transformace řetězců (C, knihovny)
 
Úkol navazuje na domácí přípravu s programy `genstr` a `strcheck`.
 
Cílem je vytvořit program, který bude řetězce generované programem `genstr` různým způsobem upravovat podle právě použité **dynamické knihovny**.
 
---
 
## 1. Program `strtransform`
 
Napište program `strtransform`, který bude číst řádky ze **standardního vstupu**, upraví je pomocí funkce z dynamické knihovny a výsledek vypíše na **standardní výstup**.
 
### Spuštění
 
```bash
./strtransform < vstup.txt
```
 
nebo například:
 
```bash
./genstr 10 5 | ./strtransform
```
 
Program načítá vstup řádek po řádku a pro každý řádek zavolá funkci:
 
```c
void transform_line(char *line);
```
 
Funkce `transform_line()` nebude implementována přímo v programu `strtransform`, ale v dynamické knihovně `libtransform.so`.
 
---
 
## 2. Dynamické knihovny
 
Vytvořte tři různé implementace dynamické knihovny `libtransform.so`.
 
Všechny knihovny budou obsahovat stejnou funkci:
 
```c
void transform_line(char *line);
```
 
Lišit se bude pouze její implementace.
 
### `upper_lib`
 
Převede všechna malá písmena `a-z` na velká.
 
Například:
 
```text
Hello World
```
 
se změní na:
 
```text
HELLO WORLD
```
 
### `lower_lib`
 
Převede všechna velká písmena `A-Z` na malá.
 
Například:
 
```text
Hello World
```
 
se změní na:
 
```text
hello world
```
 
### `swap_lib`
 
Zamění velikost každého písmene:
 
* malé písmeno převede na velké,
* velké písmeno převede na malé,
* ostatní znaky ponechá beze změny.
 
Například:
 
```text
Hello World
```
 
se změní na:
 
```text
hELLO wORLD
```
 
---
 
## 3. Statická knihovna `libcharutil.a`
 
Pomocné funkce pro práci se znaky neimplementujte zvlášť ve všech dynamických knihovnách.
 
Vytvořte funkce:
 
```c
int is_lower(char c);
int is_upper(char c);
char to_lower(char c);
char to_upper(char c);
```
 
a umístěte je do samostatného zdrojového souboru.
 
Z něj vytvořte statickou knihovnu:
 
```text
libcharutil.a
```
 
Dynamické knihovny `upper_lib/libtransform.so`, `lower_lib/libtransform.so` a `swap_lib/libtransform.so` budou tyto funkce používat.
 
Pro převod znaků nepoužívejte funkce `tolower()`, `toupper()`, `islower()` a `isupper()` z `ctype.h`.
 
---
 
## 4. Výběr dynamické knihovny
 
Program `strtransform` se nebude měnit ani znovu překládat při změně způsobu transformace.
 
Varianta se zvolí pomocí proměnné prostředí `LD_LIBRARY_PATH`.
 
Například:
 
```bash
export LD_LIBRARY_PATH=upper_lib/
./strtransform < vstup.txt
```
 
```bash
export LD_LIBRARY_PATH=lower_lib/
./strtransform < vstup.txt
```
 
```bash
export LD_LIBRARY_PATH=swap_lib/
./strtransform < vstup.txt
```
 
Ve všech adresářích se tedy nachází knihovna se stejným názvem:
 
```text
libtransform.so
```
 
a se stejným rozhraním.
 
---
 
## 5. Propojení s domácí přípravou
 
Program musí být možné použít společně s programy `genstr` a `strcheck` z domácí přípravy.
 
Například:
 
```bash
export LD_LIBRARY_PATH=upper_lib/
./genstr 10 5 | ./strtransform
```
 
Výstup programu `strtransform` musí být možné předat také programu `strcheck`.
 
Protože `strtransform` i `strcheck` používají dynamické knihovny, můžete nastavit více adresářů v `LD_LIBRARY_PATH`, například:
 
```bash
export LD_LIBRARY_PATH=upper_lib:small_lib
./genstr 10 5 | ./strtransform | ./strcheck
```
 
nebo:
 
```bash
export LD_LIBRARY_PATH=swap_lib:big_lib
./genstr 10 5 | ./strtransform | ./strcheck
```
 
Ověřte pomocí `ldd`, že programy skutečně používají požadované dynamické knihovny.
 
---
 
## 6. Makefile
 
Upravte `Makefile` tak, aby sestavil:
 
* statickou knihovnu `libcharutil.a`,
* dynamickou knihovnu `upper_lib/libtransform.so`,
* dynamickou knihovnu `lower_lib/libtransform.so`,
* dynamickou knihovnu `swap_lib/libtransform.so`,
* program `strtransform`,
* programy a knihovny z domácí přípravy.
 
Program `strtransform` musí být linkován s dynamickou knihovnou pomocí:
 
```bash
-L... -ltransform
```
 
Statická knihovna musí být linkována pomocí:
 
```bash
-L... -lcharutil
```
 
Nepoužívejte při linkování přímo cestu k souboru `.a` nebo `.so`.
 
Makefile musí obsahovat alespoň cíle:
 
```text
all
clean
test
```
 
Při překladu používejte alespoň následující volby:
 
```text
-Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer
```
 
Volbu:
 
```text
-fsanitize=address
```
 
použijte také při **linkování výsledných programů a dynamických knihoven**.
 
AddressSanitizer umožňuje při spuštění programu odhalit například:
 
* přístup mimo vyhrazenou paměť,
* použití paměti po jejím uvolnění,
* některé chyby při práci s dynamicky alokovanou pamětí.
 
Pokud sanitizér při běhu programu vypíše chybu, považujte ji za chybu programu a opravte ji.
 
---
 
## 7. Testy
 
Cíl:
 
```bash
make test
```
 
ověří alespoň všechny tři varianty knihovny `libtransform.so`.
 
Pro vstup:
 
```text
Hello World
AbC xYz
```
 
musí vzniknout:
 
### `upper_lib`
 
```text
HELLO WORLD
ABC XYZ
```
 
### `lower_lib`
 
```text
hello world
abc xyz
```
 
### `swap_lib`
 
```text
hELLO wORLD
aBc XyZ
```
 
Test vypíše pro jednotlivé varianty výsledek:
 
```text
upper: PASS
lower: PASS
swap: PASS
```
 
nebo `FAIL`.
 
---
 
## Odevzdání
 
Odevzdejte:
 
* zdrojové soubory `.c` a `.h`,
* `Makefile`,
* případný testovací skript.
 
Neodevzdávejte přeložené soubory:
 
```text
.o
.a
.so
```