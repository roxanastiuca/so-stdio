Nume: STIUCA Roxana-Elena
Grupa: 335CB

# Tema 2 SO - Biblioteca stdio

### Organizare
Tipul SO_FILE descrie un fisier deschis si are urmatoarele campuri:
- handle = file handler pentru fisier deschis;
- process = informatia despre un proces lansat prin popen;
- rbuffer = buffer pentru citire;
- roffset = pozitia din buffer-ul de citire pana unde utilizatorul a citit
efectiv;
- rsize = dimensiunea bufferului de citire (numarul de bytes utili);
- rerror = flag care retine daca operatia read a avut succes sau nu;
- wbuffer = buffer pentru scriere;
- woffset = pozitia din buffer-ul de scriere pana unde s-a scris;
- werror = flag care retine daca operatia write a avut succes sau nu.

Functiile implementate opereaza pe un obiect SO_FILE.

### Implementare
Este implementat intreg enuntul.

#### Buffering
Operatiile de citire/scriere se fac prin intermediul unui buffer.
Daca s-a ajuns la finalul bufferului de citire, atunci se realizeaza
un alt apel read pentru a reumple bufferul. Similar, daca bufferul
de scriere a fost umplut cu date, se realizeaza un apel write pentru
a-l goli. Inainte sa inchidem un fisier, bufferul de scriere trebuie
golit.

#### Pozitia cursorului in fisier
In cazul operatiei fseek, este golit bufferul de scriere, iar bufferul
de citire este invalidat (s-a citit in avans).
Operatia ftell obtine pozitia curenta a cursorului, facand un apel
lseek din SEEK_CUR cu offset 0. Ca in cazul fseek, trebuie invalidata
zona citita in avans si validata zona scrisa momentan doar in bufferul
de scriere.

#### Rulare de procese
Pasii popen sunt: creare pipe, creare proces, inchidere capete pipe
nefolosite si redirectare STDIN/STDOUT, lansare comanda.
Operatia pclose goleste bufferul de scriere, dezaloca memoria si
asteapta terminarea procesului lansat de popen.

### Cum se compileaza si cum se ruleaza?
**Creare biblioteca dinamica**:
- Windows - nmake.

### Git
https://github.com/roxanastiuca/so-stdio
