#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>
#include <algorithm>
#include <curses.h>

using namespace std;


/*
 * Deklaruje zmienne do przechowywania zakresów czasu dla myślenia i jedzenia.
 * Wartości zostaną ustawione w funkcji main() w zależności od wybranego trybu symulacji.
 */
int CZAS_JEDZENIA_MIN_MS;
int CZAS_JEDZENIA_MAX_MS;
int CZAS_MYSLENIA_MIN_MS;
int CZAS_MYSLENIA_MAX_MS;

/* Stała określająca liczbę filozofów przy stole. */
const int LICZBA_FILOZOFOW = 5;


/* Pamięć Współdzielona
 * Tablica muteksów reprezentujących pałeczki. Każdy mutex to "zamek",
 * który może być zablokowany tylko przez jeden wątek (filozofa) naraz.
 * Dostęp do pałeczki[i] chroni i-tą pałeczkę.
 */
mutex paleczki[LICZBA_FILOZOFOW];

// Typ wyliczeniowy (enum class) definiujący możliwe stany filozofa.
enum class StanFilozofa { MYSLI, GLODNY, JE };
// Tablica przechowująca aktualny stan każdego filozofa. Używam do wyswietlania
StanFilozofa stanyFilozofow[LICZBA_FILOZOFOW];
// Tablica przechowująca ID filozofa, który trzyma daną pałeczkę (-1 = wolna).
int wlascicielePaleczek[LICZBA_FILOZOFOW];
// Tablica przechowująca imiona filozofów.
string imionaFilozofow[LICZBA_FILOZOFOW] = { "Shrek", "Fiona", "Osiol", "Kot", "Smoczyca" };

/*
 * Tablica atomowych liczników posiłków. `atomic<int>` gwarantuje, że operacja
 * `licznik++` jest bezpieczna wątkowo (nie wystąpi wyścig danych), nawet jeśli
 * wątek główny odczytuje licznik (`.load()`) w tym samym czasie.
 */
atomic<int> licznikPosilkow[LICZBA_FILOZOFOW];

/*
 * Mutex chroniący dostęp do tablic `stanyFilozofow` i `wlascicielePaleczek`.
 * Zapobiega sytuacji, w której wątek główny odczytuje te tablice podczas ich modyfikacji
 * przez wątki filozofów.
 */
mutex mutexStanu;
/*
 * Atomowa flaga logiczna kontrolująca działanie wszystkich pętli `while` w programie.
 * Ustawienie jej na `false` w wątku głównym powoduje bezpieczne zakończenie
 * wszystkich wątków filozofów i pętli rysowania.
 */
atomic<bool> symulacjaDziala{true};


//FUNKCJE POMOCNICZE


/*
 * Losuje czas z zakresu podanego wcześńiej
 */
int losujCzas(int min_ms, int max_ms) {
    // `thread_local` tworzy oddzielną instancję generatora dla każdego wątku.
    thread_local mt19937 generator(random_device{}()); // Ziarno inicjalizowane losowo przy pierwszym wywołaniu w danym wątku.
    uniform_int_distribution<int> dystrybucja(min_ms, max_ms); // Równomierny rozkład w zakresie.
    return dystrybucja(generator); // Zwraca kolejną liczbę z sekwencji generatora.
}


/*
 * Zmienia na ladny tekst
 */
string stanNaString(StanFilozofa stan) {
    switch (stan) {
        case StanFilozofa::MYSLI:   return "MYSLI";
        case StanFilozofa::GLODNY:  return "GLODNY";
        case StanFilozofa::JE:      return "JE";
    }
    return "?";
}


// Aktualizuje bezpiecznie stan filozofa

void ustawStanFilozofa(int id, StanFilozofa stan) {
    // `lock_guard` blokuje `mutexStanu` przy tworzeniu obiektu `blokada`.
    lock_guard<mutex> blokada(mutexStanu);
    stanyFilozofow[id] = stan;
    // `mutexStanu` jest automatycznie odblokowywany, gdy `blokada` wychodzi poza zakres (koniec funkcji).
}
// Ustawai kto jest wlasciecielem pałeczki

void ustawWlascicielaPaleczki(int idPaleczki, int idFilozofa) {
    lock_guard<mutex> blokada(mutexStanu);
    wlascicielePaleczek[idPaleczki] = idFilozofa;
}
// Symuluje mylsenie z podanego wcześneij zakresu

void mysl(int id) {
    ustawStanFilozofa(id, StanFilozofa::MYSLI);
    int czasMyslenia = losujCzas(CZAS_MYSLENIA_MIN_MS, CZAS_MYSLENIA_MAX_MS);
    this_thread::sleep_for(chrono::milliseconds(czasMyslenia));
}


//Symuluje jedzenie z czasu wcześneij podanego

void jedz(int id) {
    ustawStanFilozofa(id, StanFilozofa::JE);
    /* Zwiększ atomowy licznik posiłków dla tego filozofa. Operacja `++` jest bezpieczna wątkowo. */
    licznikPosilkow[id]++;
    int czasJedzenia = losujCzas(CZAS_JEDZENIA_MIN_MS, CZAS_JEDZENIA_MAX_MS);
    this_thread::sleep_for(chrono::milliseconds(czasJedzenia));
}




/*Zaklezczenie wytępuje wtdy gdy wątek czeka na inne zasoby które  zajęte są przez inny wątek i w ten sposób się blokują.
 * Funkcja przyjmuj id filozofa. Żeby była możłliwość zakleszczenia musza być 4 warunki coffmana z wikipenii
 *
 * Wzajemne wykluczanie (Mutual Exclusion): Pałeczka (mutex) może być trzymana tylko przez jednego filozofa naraz. Gwarantuje to mutex::lock().

Trzymanie i oczekiwanie (Hold and Wait): Filozof trzyma już jedną pałeczkę (paleczki[lewa].lock()) i czeka na drugą (paleczki[prawa].lock()). Nie odkłada pierwszej, jeśli druga jest zajęta. To kluczowe.

Brak wywłaszczania (No Preemption): Pałeczki nie można filozofowi "wyrwać". Może ją zwolnić tylko on sam, dobrowolnie (unlock()), po zakończeniu jedzenia.

Czekanie cykliczne (Circular Wait): To jest właśnie skutek połączenia powyższych warunków z pechową synchronizacją (którą wymuszamy krótkim czasem myślenia i pauzą sleep(100)). Tworzy się zamknięty krąg:

P0 (trzyma P0) czeka na P1 (trzymaną przez P1)

P1 (trzyma P1) czeka na P2 (trzymaną przez P2)

P2 (trzyma P2) czeka na P3 (trzymaną przez P3)

P3 (trzyma P3) czeka na P4 (trzymaną przez P4)

P4 (trzyma P4) czeka na P0 (trzymaną przez P0)
 */
void Zakleszczenie_Filozofowie(int id) {
    //jaką pałeczkę potrzebuje
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;
    //Działanie symulacji dopóki w main nie będzie false czyli przycisk q
    while (symulacjaDziala) {
        //ustawaimy myslenie i filozof myśli przez jakis czas
        mysl(id);
        //filozof jest głodny
        ustawStanFilozofa(id, StanFilozofa::GLODNY);

        //paleczka leewa jest pobierana pierwsza jeśli to możliwe i blokuuje ja a jesli nie to czeka aż będzie wolna
        paleczki[lewa].lock(); // Czeka, jeśli zajęta
        ustawWlascicielaPaleczki(lewa, id);

        /*sleep dodajemy jesli chcemy żeby ta predkość komputra została zniwelowana czyli cały czs się blokowały bo czas reakcji komputera jest bardzo szybki
            sleep daje czas innym filozofom żeby też podniesli pierwsza pałeczkę najpeirw
        */
        //this_thread::sleep_for(chrono::milliseconds(100));
        /*
         * Filozof bierze prawą pałeczkę jeśli ma już lewa i jeśli prawa jst owlna jeśli nie jes wolna to trzyma lewa i czeka aż bezie prawa zwolniona
         */


        paleczki[prawa].lock();
        ustawWlascicielaPaleczki(prawa, id);

        //Je jak ma 2 pałeczki
        jedz(id);

        // oddanie pałeczek
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock();
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock();
    }
}


/*
 *  W zagłodzeniu używam try_lock() żeby spróbować podnieść pałeczkę jeśli się nie uuda
 * to próbuje ponownie  co może powodować że jeden filozof będzie całyc zas
 * wyprzedzany w podnoszeniu pałeczek przez inych filozofów i bdzie mniej jadł.
 *
 */


void Zaglodzenie_Filozofowie(int id) {
    // jakei pałeczki podnosi jak np id =4 to paleczka 4 i 5 mod 5 czyli 0
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;

    while (symulacjaDziala) {

        // ETAP MYŚLENIA
        //    Filozof myśli przez losowy czas (2-5 sekund) żeby ich rozsynchornizować
        mysl(id);

        //Jest głodny
        ustawStanFilozofa(id, StanFilozofa::GLODNY);

        // Ustawaimy flagę czy już zjadł czy nie  jeśli nie to cały czas próbuje jesc dopóki zjadł = true albo symulacja się nie skończy
        bool zjadl = false;

        /*
         * Pętla "spinująca".
         * Pętla wykonuje się bardzo szybko
         * dopóki filozofowi nie uda się zjeść (zjadl == true) lub
         * dopóki symulacja się nie zakończy.
         */
        while (!zjadl && symulacjaDziala) {

            // PIERWSZA PRÓBA: Sięgnij po lewą pałeczkę
            //    `try_lock()` to funkcja "Spróbuj zamknąć".
            //    - Jeśli pałeczka jest WOLNA: Zamyka ją i zwraca `true`.
            //    - Jeśli pałeczka jest ZAJĘTA: NIE CZEKA. Zwraca `false`.
            if (paleczki[lewa].try_lock()) {

                ustawWlascicielaPaleczki(lewa, id);

                // DRUGA PRÓBA: (Trzymając lewą) Sięgnij po prawą pałeczkę
                if (paleczki[prawa].try_lock()) {

                    ustawWlascicielaPaleczki(prawa, id);

                    //  ETAP JEDZENIA jeśli zdobyliśmy obnie pałeczki
                    jedz(id); // Je przez losowy czas

                    // WYJŚCIE Z PĘTLI
                    //     Ustawia flagę na `true`. Pętla 'while(!zjadl)'
                    //     zakończy się przy następnym sprawdzeniu.
                    zjadl = true;
                    //odklada paleczki
                    ustawWlascicielaPaleczki(prawa, -1);
                    paleczki[prawa].unlock();

                } else {
                    //  (Porażka z prawą) Mamy lewą, ale prawa była zajęta.
                    //     Nie możemy jeść. Musimy odłożyć lewą.
                    ustawWlascicielaPaleczki(lewa, -1);
                }


                paleczki[lewa].unlock();
            }


            /*Filozof jest "uparty" próbuje ponownie od frazu.
             * I znowu. I znowu. Tysiące razy na sekundę.
            To jest "spinowanie", które marnuje jego czas procesora.
             */
        }

        // KONIEC CYKLU
        // Filozof wydostaje się z pętli 'while(!zjadl)' tylko wtedy, gdy udało mu się zjeść (krok 10).
        //Teraz wraca na początek pętli życia (krok 2) i idzie myśleć.
    }
}


/*
 * Zeby nie wystapiło zaglodznie ani zakleszczenie wystarczy że zrobimy prostą amianę filozofowie o parzystym indeksie
 * najpierw próbują wziąć prawą pałeczkę a ci o nieparzystym najpierw po lewą. Używamy też lock czyli czeka aż bęzei wolna nie narnuje procesora i w końcu kiedyś dostanie dostę do pałeczki czyli nie będzie zagłodzneia
 */
void Asymetria_Filozofowie(int id) {
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;
    while (symulacjaDziala) {
        mysl(id);
        ustawStanFilozofa(id, StanFilozofa::GLODNY);
        if (id % 2 == 0) {
            paleczki[prawa].lock();
            ustawWlascicielaPaleczki(prawa, id);
            paleczki[lewa].lock();
            ustawWlascicielaPaleczki(lewa, id);
        } else {
            paleczki[lewa].lock();
            ustawWlascicielaPaleczki(lewa, id);
            paleczki[prawa].lock();
            ustawWlascicielaPaleczki(prawa, id);
        }
        jedz(id);
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock();
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock();
    }
}
/*
 * metoda Hierwarchii żeby uniknąć zakleszczenia, filozof zawsze podnosi pałeczkę
 * o niższym numerze ID jako pierwszą, a potem tę o wyższym numerze ID.
 * Używa blokującej funkcji lock(), co zapobiega zagłodzeniu.
 */
void Hierarchia_Filozofowie(int id) {
    /*
     * Określenie indeksów potrzebnych pałeczek.
     */
    int paleczka1_id = id;
    int paleczka2_id = (id + 1) % LICZBA_FILOZOFOW;

    /*
     * Ustalenie hierarchii podnoszenia.
     * Używamy funkcji min() i max(), aby zawsze wiedzieć, którą podnieść jako pierwszą.
     */
    int pierwsza_paleczka_id = min(paleczka1_id, paleczka2_id);
    int druga_paleczka_id = max(paleczka1_id, paleczka2_id);

    /*
     * Pętla życia filozofa.
     */
    while (symulacjaDziala) {

         //Myślenie.

        mysl(id);


         //Jest głodny.

        ustawStanFilozofa(id, StanFilozofa::GLODNY);

        /*
         * Podnoszenie pałeczek ZGODNIE Z HIERARCHIĄ.
         * Zawsze blokujemy najpierw mutex pałeczki o niższym ID.
         * Funkcja lock() jest blokująca - wątek czeka, jeśli pałeczka jest zajęta.
         */
        paleczki[pierwsza_paleczka_id].lock();
        ustawWlascicielaPaleczki(pierwsza_paleczka_id, id);

        /*Podniesienie drugiej pałeczki (o wyższym ID).
         * Wątek wciąż trzyma pierwszą pałeczkę.
         */
        paleczki[druga_paleczka_id].lock();
        ustawWlascicielaPaleczki(druga_paleczka_id, id);

        /*Jedzenie.
         * Filozof ma obie pałeczki.
         */
        jedz(id);

        /*
         *  Odkładanie pałeczek.
         */
        ustawWlascicielaPaleczki(druga_paleczka_id, -1);
        paleczki[druga_paleczka_id].unlock();

        ustawWlascicielaPaleczki(pierwsza_paleczka_id, -1);
        paleczki[pierwsza_paleczka_id].unlock();


    }
}

int main() {
    // Menu wyboru logiki
    int wyborLogiki = 0;
    cout << "Wybierz logike dzialania filozofow:" << endl;
    cout << "  1. Zakleszczenie (naiwna)" << endl;
    cout << "  2. Zaglodzenie (try_lock)" << endl;
    cout << "  3. Poprawna (asymetryczna)" << endl;
    cout << "  4. Poprawna (hierarchia zasobow)" << endl;
    while (true) {
        cout << "Wybor (1, 2, 3, 4): ";
        cin >> wyborLogiki;
        if (wyborLogiki >= 1 && wyborLogiki <= 4) {
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Ignoruj resztę linii, w tym Enter
            break;
        }
        cout << "Niepoprawny wybor." << endl;
        // Czyszczenie bufora cin na wypadek błędnego wejścia
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }


    // Inicjalizacja biblioteki ncurses
    if (initscr() == NULL) {
        // Jeśli inicjalizacja się nie powiodła, wypisz błąd i zakończ
        fprintf(stderr, "Blad podczas inicjalizacji ncurses.\n");
        return 1;
    }
    noecho();               // Nie wyświetlaj wciskanych klawiszy
    cbreak();               // Reaguj na klawisze natychmiast (bez buforowania linii)
    nodelay(stdscr, TRUE);  // Funkcja getch() nie będzie czekać na klawisz (tryb nieblokujący)
    curs_set(0);            // Ukryj kursor terminala

    // Inicjalizacja stanów początkowych filozofów i pałeczek oraz liczników
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        stanyFilozofow[i] = StanFilozofa::MYSLI; // Wszyscy zaczynają myśleć
        wlascicielePaleczek[i] = -1; // Wszystkie pałeczki są wolne
        licznikPosilkow[i].store(0); // Wyzeruj atomowe liczniki
    }

    // Ustawienie zakresów czasu w zależności od wybranego trybu
    switch (wyborLogiki) {
        case 1:
            cout << "Tryb 1 (Zakleszczenie)" << endl;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 2000;
            CZAS_JEDZENIA_MIN_MS = 2000;
            CZAS_JEDZENIA_MAX_MS = 5000;
            break;

        case 2:
            cout << "Tryb 2 (Zaglodzenie)" << endl;
            CZAS_JEDZENIA_MIN_MS = 1000;
            CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 5000;
            break;

        case 3:
            cout << "Tryb 3 (Poprawny)" << endl;
            CZAS_JEDZENIA_MIN_MS = 1000;
            CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 5000;
            break;
        case 4: //
            cout << "Tryb 4 (Poprawny - Hierarchia)" << endl;
            CZAS_JEDZENIA_MIN_MS = 1000;
            CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 5000;
            break;
    }

    // Uruchamianie 5 wątków filozofów
    thread watkiFilozofow[LICZBA_FILOZOFOW];

    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        // Wybierz funkcję logiki na podstawie wyboru użytkownika
        switch (wyborLogiki) {
            case 1: watkiFilozofow[i] = thread(Zakleszczenie_Filozofowie, i); break;
            case 2: watkiFilozofow[i] = thread(Zaglodzenie_Filozofowie, i); break;
            case 3: watkiFilozofow[i] = thread(Asymetria_Filozofowie, i); break;
            case 4: watkiFilozofow[i] = thread(Hierarchia_Filozofowie, i); break;
        }
    }

    /* Główna pętla programu (rysowanie stanu i obsługa wejścia w main)
     * Zmienne lokalne do przechowywania kopii stanu
     * Pętla działa dopóki użytkownik nie naciśnie 'q'
     */
    vector<StanFilozofa> stany_kopia(LICZBA_FILOZOFOW);
    vector<int> liczniki_kopia(LICZBA_FILOZOFOW);
    vector<int> wlasciciele_kopia(LICZBA_FILOZOFOW);

    while (symulacjaDziala) {
        /* Kopiuje aktualny stan globalny do zmiennych lokalnych (pod muteksem) */
        {
            lock_guard<mutex> blokada(mutexStanu);
            for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
                stany_kopia[i] = stanyFilozofow[i];
                liczniki_kopia[i] = licznikPosilkow[i].load();
                wlasciciele_kopia[i] = wlascicielePaleczek[i];
            }
        }

        /* Rysuje stan na ekranie ncurses */
        erase(); /* Wyczyść bufor ekranu ncurses */
        mvprintw(0, 0, "-----------------PROBLEM UCZTUJACYCH FILOZOFOW ------------------");
        mvprintw(2, 0, "ID"); mvprintw(2, 5, "Filozof"); mvprintw(2, 18, "Stan");
        mvprintw(2, 28, "L. Paleczka"); mvprintw(2, 44, "P. Paleczka"); mvprintw(2, 60, "Zjadl");

        for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
            int y = 4 + i;
            mvprintw(y, 0, "%d", i);
            mvprintw(y, 5, "%s", imionaFilozofow[i].c_str());
            mvprintw(y, 18, "%s", stanNaString(stany_kopia[i]).c_str());

            stringstream ss_lewa, ss_prawa;
            int lewa_id = i;
            if (wlasciciele_kopia[lewa_id] == -1) ss_lewa << "WOLNA";
            else if (wlasciciele_kopia[lewa_id] == i) ss_lewa << "Trzyma";
            else ss_lewa << "Zajeta(" << wlasciciele_kopia[lewa_id] << ")";
            mvprintw(y, 28, "%s", ss_lewa.str().c_str());

            int prawa_id = (i + 1) % LICZBA_FILOZOFOW;
            if (wlasciciele_kopia[prawa_id] == -1) ss_prawa << "WOLNA";
            else if (wlasciciele_kopia[prawa_id] == i) ss_prawa << "Trzyma";
            else ss_prawa << "Zajeta(" << wlasciciele_kopia[prawa_id] << ")";
            mvprintw(y, 44, "%s", ss_prawa.str().c_str());

            mvprintw(y, 60, "%d", liczniki_kopia[i]);
        }
        mvprintw(LINES - 1, 0, "Nacisnij 'q' aby zakonczyc...");

        /* Sprawdzam klawiaturę */
        int ch = getch();

        // Tymczasowo pokazuje, co odczytał getch() (jeśli nie ERR)
        if (ch != ERR) {
            mvprintw(LINES - 2, 0, "Odczytano klawisz: %d ('%c')", ch, ch);
        } else {
            // Wyczyść poprzedni komunikat, jeśli nic nie naciśnięto
            mvprintw(LINES - 2, 0, "Odczytano klawisz: ERR     ");
        }

        if (ch == 'q') {
            symulacjaDziala = false; /* Ustaw flagę zakończenia */
        }

        //POkazuje wszystko na ekranie Aktualizuj fizyczny ekran terminala
        refresh();

        /* Czekam przed następnym odświeżeniem */
        napms(100);

    } /* Koniec głównej pętli działania symulacji */


    /* Czekanie, aż wszystkie wątki filozofów zakończą swoją pracę */
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        watkiFilozofow[i].join();
    }

    // Zakończenie pracy z biblioteką ncurses i przywrócenie normalnego terminala
    curs_set(1);            //Pokaż z powrotem kursor
    nocbreak();             //Wyłącz tryb cbreak
    echo();                 //Włącz z powrotem echo (wyświetlanie wciskanych klawiszy)
    endwin();               //Zakończ tryb ncurses

    // Wyświetlenie podsumowania w standardowej konsoli po zamknięciu już ncurses
    cout << "Symulacja zakonczona." << endl;
    cout << "\n--- OSTATECZNE PODSUMOWANIE POSILKOW ---" << endl;
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        cout << "  " << setw(10) << left << imionaFilozofow[i]
             << " (" << i << "): zjadl " << licznikPosilkow[i].load() << " razy." << endl;
    }

    return 0;
}