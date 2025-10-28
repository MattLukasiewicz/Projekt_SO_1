#include <iostream>     // Do std::cout, std::cin
#include <thread>       // Do std::thread
#include <mutex>        // Do std::mutex, std::lock_guard
#include <chrono>       // Do std::chrono::milliseconds, std::chrono::seconds
#include <random>       // Do std::random_device, std::mt19937, std::uniform_int_distribution
#include <string>       // Do std::string
#include <atomic>       // Do std::atomic<int>, std::atomic<bool>
#include <iomanip>      // Do std::setw, std::left, std::right
#include <limits>       // Do std::numeric_limits
#include <sstream>      // Do std::stringstream
#include <vector>       // Do std::vector
#include <algorithm>    // Do std::min, std::max
#include <curses.h>     // Do ncurses (initscr, mvprintw, refresh, etc.)

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


/* --- Pamięć Współdzielona --- */
/*
 * Tablica muteksów reprezentujących pałeczki. Każdy mutex to "zamek",
 * który może być zablokowany tylko przez jeden wątek (filozofa) naraz.
 * Dostęp do pałeczki[i] chroni i-tą pałeczkę.
 */
mutex paleczki[LICZBA_FILOZOFOW];

/* Typ wyliczeniowy (enum class) definiujący możliwe stany filozofa. Poprawia czytelność. */
enum class StanFilozofa { MYSLI, GLODNY, JE };
/* Tablica przechowująca aktualny stan każdego filozofa. Używana do wyświetlania. */
StanFilozofa stanyFilozofow[LICZBA_FILOZOFOW];
/* Tablica przechowująca ID filozofa, który trzyma daną pałeczkę (-1 = wolna). Używana do wyświetlania. */
int wlascicielePaleczek[LICZBA_FILOZOFOW];
/* Tablica przechowująca imiona filozofów. */
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


/* --- FUNKCJE POMOCNICZE --- */


/**
 * @brief Zwraca losową liczbę całkowitą z podanego zakresu (włącznie).
 * @param min_ms Dolna granica zakresu.
 * @param max_ms Górna granica zakresu.
 * @return Wylosowana liczba milisekund.
 * @note Używa `thread_local` generatora, aby zapewnić bezpieczeństwo wątkowe
 * i unikalną sekwencję losową dla każdego wątku.
 */
int losujCzas(int min_ms, int max_ms) {
    /* `thread_local` tworzy oddzielną instancję generatora dla każdego wątku. */
    thread_local mt19937 generator(random_device{}()); /* Ziarno inicjalizowane losowo przy pierwszym wywołaniu w danym wątku. */
    uniform_int_distribution<int> dystrybucja(min_ms, max_ms); /* Równomierny rozkład w zakresie. */
    return dystrybucja(generator); /* Zwraca kolejną liczbę z sekwencji generatora. */
}


/**
 * @brief Konwertuje wartość enum StanFilozofa na czytelny ciąg znaków.
 * @param stan Stan filozofa do konwersji.
 * @return String reprezentujący stan ("MYSLI", "GLODNY", "JE").
 */
string stanNaString(StanFilozofa stan) {
    switch (stan) {
        case StanFilozofa::MYSLI:   return "MYSLI";
        case StanFilozofa::GLODNY:  return "GLODNY";
        case StanFilozofa::JE:      return "JE";
    }
    return "?"; /* Na wypadek nieznanej wartości */
}


/**
 * @brief Bezpiecznie wątkowo aktualizuje stan filozofa w globalnej tablicy.
 * @param id ID filozofa, którego stan jest aktualizowany.
 * @param stan Nowy stan filozofa.
 * @note Używa `lock_guard` do automatycznego zarządzania blokadą `mutexStanu`.
 */
void ustawStanFilozofa(int id, StanFilozofa stan) {
    /* `lock_guard` blokuje `mutexStanu` przy tworzeniu obiektu `blokada`. */
    lock_guard<mutex> blokada(mutexStanu);
    stanyFilozofow[id] = stan;
    /* `mutexStanu` jest automatycznie odblokowywany, gdy `blokada` wychodzi poza zakres (koniec funkcji). */
}
/**
 * @brief Bezpiecznie wątkowo aktualizuje właściciela pałeczki w globalnej tablicy.
 * @param idPaleczki ID pałeczki, której właściciel jest aktualizowany.
 * @param idFilozofa ID nowego właściciela (-1 oznacza brak właściciela).
 * @note Używa `lock_guard` do automatycznego zarządzania blokadą `mutexStanu`.
 */
void ustawWlascicielaPaleczki(int idPaleczki, int idFilozofa) {
    lock_guard<mutex> blokada(mutexStanu);
    wlascicielePaleczek[idPaleczki] = idFilozofa;
}
/**
 * @brief Symuluje proces myślenia filozofa.
 * Ustawia stan na MYSLI i usypia wątek na losowy czas.
 * @param id ID filozofa, który myśli.
 */
void mysl(int id) {
    ustawStanFilozofa(id, StanFilozofa::MYSLI); /* Zaktualizuj stan (bezpiecznie). */
    int czasMyslenia = losujCzas(CZAS_MYSLENIA_MIN_MS, CZAS_MYSLENIA_MAX_MS); /* Wylosuj czas myślenia. */
    /* Uśpij *tylko ten* wątek na określony czas. */
    this_thread::sleep_for(chrono::milliseconds(czasMyslenia));
}


/**
 * @brief Symuluje proces jedzenia filozofa.
 * Ustawia stan na JE, zwiększa licznik posiłków i usypia wątek na losowy czas.
 * @param id ID filozofa, który je.
 */
void jedz(int id) {
    ustawStanFilozofa(id, StanFilozofa::JE); /* Zaktualizuj stan (bezpiecznie). */
    /* Zwiększ atomowy licznik posiłków dla tego filozofa. Operacja `++` jest bezpieczna wątkowo. */
    licznikPosilkow[id]++;
    int czasJedzenia = losujCzas(CZAS_JEDZENIA_MIN_MS, CZAS_JEDZENIA_MAX_MS); /* Wylosuj czas jedzenia. */
    /* Uśpij *tylko ten* wątek na określony czas. */
    this_thread::sleep_for(chrono::milliseconds(czasJedzenia));
}



/**
 * @brief Logika działania filozofa implementująca naiwne podejście (lewa->prawa),
 * które może prowadzić do zakleszczenia (deadlock).
 * @param id ID filozofa.
 * @note Warunki zakleszczenia (Coffmana): Wzajemne wykluczanie (mutex),
 * Trzymanie i oczekiwanie (lock lewej, czeka na prawą), Brak wywłaszczania (unlock po jedzeniu),
 * Czekanie cykliczne (możliwe przy synchronicznym starcie).
 */
void Zakleszczenie_Filozofowie(int id) {
    /* Określenie ID potrzebnych pałeczek */
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;
    /* Główna pętla życia */
    while (symulacjaDziala) {
        mysl(id); /* Faza myślenia */
        ustawStanFilozofa(id, StanFilozofa::GLODNY); /* Faza głodu */

        /* Krok 1: Podnieś lewą pałeczkę (blokująco) */
        paleczki[lewa].lock(); /* Czeka, jeśli zajęta */
        ustawWlascicielaPaleczki(lewa, id); /* Zaktualizuj tablicę stanu */

        /**
         * @brief Opcjonalna pauza zwiększająca szansę na zakleszczenie.
         * Daje innym wątkom czas na podniesienie ich lewych pałeczek,
         * zanim ten wątek spróbuje podnieść prawą.
         * Dla pewnej demonstracji deadlocka, odkomentuj tę linię i ustaw
         * krótki, stały czas myślenia w main().
         */
        //this_thread::sleep_for(chrono::milliseconds(100));

        /* Krok 2: Podnieś prawą pałeczkę (blokująco, trzymając lewą) */
        paleczki[prawa].lock(); /* Czeka, jeśli zajęta */
        ustawWlascicielaPaleczki(prawa, id); /* Zaktualizuj tablicę stanu */

        /* Krok 3: Jedzenie (tylko po zdobyciu obu pałeczek) */
        jedz(id);

        /* Krok 4: Odkładanie pałeczek */
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock(); /* Zwolnij prawą */
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock(); /* Zwolnij lewą */
    }
}


/**
 * @brief Logika działania filozofa demonstrująca zagłodzenie poprzez livelock ("spinowanie").
 * @param id ID filozofa.
 * @note Filozof używa nieblokującego `try_lock()`. Jeśli próba zawiedzie, natychmiast
 * próbuje ponownie, marnując czas procesora i potencjalnie będąc ciągle
 * wyprzedzanym przez "szczęśliwszych" sąsiadów.
 */
void Zaglodzenie_Filozofowie(int id) {
    /* Określenie ID potrzebnych pałeczek */
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;

    /* Główna pętla życia */
    while (symulacjaDziala) {
        mysl(id); /* Faza myślenia */
        ustawStanFilozofa(id, StanFilozofa::GLODNY); /* Faza głodu */

        /* Flaga kontrolująca pętlę prób */
        bool zjadl = false;

        /* Pętla prób zdobycia pałeczek ("spinująca") */
        while (!zjadl && symulacjaDziala) {

            /* Krok 1: Spróbuj zdobyć lewą pałeczkę (nieblokująco) */
            if (paleczki[lewa].try_lock()) { /* Zwraca true jeśli się udało, false jeśli zajęta */
                ustawWlascicielaPaleczki(lewa, id); /* Sukces - aktualizuj stan */

                /* Krok 2: Spróbuj zdobyć prawą pałeczkę (nieblokująco, trzymając lewą) */
                if (paleczki[prawa].try_lock()) {
                    /* Sukces z obiema! */
                    ustawWlascicielaPaleczki(prawa, id);
                    jedz(id); /* Jedz */
                    zjadl = true; /* Ustaw flagę, by wyjść z pętli prób */
                    /* Odłóż prawą */
                    ustawWlascicielaPaleczki(prawa, -1);
                    paleczki[prawa].unlock();
                } else {
                    /* Porażka z prawą. Musimy odłożyć lewą. */
                    ustawWlascicielaPaleczki(lewa, -1);
                }
                /* Odłóż lewą (albo po jedzeniu, albo po porażce z prawą) */
                paleczki[lewa].unlock();
            }
            /* Jeśli porażka nastąpiła w kroku 1 (lewa zajęta) lub w kroku 2 (prawa zajęta),
               flaga 'zjadl' jest 'false'. Pętla 'while' natychmiast się powtarza,
               powodując "spinowanie". */
        }
        /* Koniec pętli prób - udało się zjeść */
    }
}


/**
 * @brief Logika działania filozofa implementująca rozwiązanie asymetryczne (poprawne).
 * @param id ID filozofa.
 * @note Filozofowie z parzystym ID podnoszą najpierw prawą pałeczkę,
 * a z nieparzystym ID - najpierw lewą. Przerywa to cykl zależności.
 * Używa blokującego `lock()`, co zapobiega zagłodzeniu.
 */
void Asymetria_Filozofowie(int id) {
    /* Określenie ID potrzebnych pałeczek */
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;
    /* Główna pętla życia */
    while (symulacjaDziala) {
        mysl(id); /* Myślenie */
        ustawStanFilozofa(id, StanFilozofa::GLODNY); /* Głód */

        /* Asymetria w podnoszeniu pałeczek */
        if (id % 2 == 0) { /* Filozof parzysty */
            paleczki[prawa].lock(); /* 1. Prawa (blokująco) */
            ustawWlascicielaPaleczki(prawa, id);
            paleczki[lewa].lock();  /* 2. Lewa (blokująco) */
            ustawWlascicielaPaleczki(lewa, id);
        } else { /* Filozof nieparzysty */
            paleczki[lewa].lock();  /* 1. Lewa (blokująco) */
            ustawWlascicielaPaleczki(lewa, id);
            paleczki[prawa].lock(); /* 2. Prawa (blokująco) */
            ustawWlascicielaPaleczki(prawa, id);
        }

        jedz(id); /* Jedzenie */

        /* Odkładanie pałeczek */
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock();
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock();
    }
}
/**
 * @brief Logika działania filozofa implementująca hierarchię zasobów (poprawne).
 * @param id ID filozofa.
 * @note Filozof zawsze podnosi pałeczkę o niższym numerze jako pierwszą,
 * a potem tę o wyższym numerze. Przerywa to cykl zależności.
 * Używa blokującego `lock()`, co zapobiega zagłodzeniu.
 */
void Hierarchia_Filozofowie(int id) {
    /* Określenie ID potrzebnych pałeczek */
    int paleczka1_id = id;
    int paleczka2_id = (id + 1) % LICZBA_FILOZOFOW;

    /* Ustalenie hierarchii - która ma niższy, a która wyższy numer */
    int pierwsza_paleczka_id = min(paleczka1_id, paleczka2_id);
    int druga_paleczka_id = max(paleczka1_id, paleczka2_id);

    /* Główna pętla życia */
    while (symulacjaDziala) {
        mysl(id); /* Myślenie */
        ustawStanFilozofa(id, StanFilozofa::GLODNY); /* Głód */

        /* Podnoszenie zgodnie z hierarchią (niższy numer pierwszy) */
        paleczki[pierwsza_paleczka_id].lock(); /* 1. Niższa ID (blokująco) */
        ustawWlascicielaPaleczki(pierwsza_paleczka_id, id);

        paleczki[druga_paleczka_id].lock(); /* 2. Wyższa ID (blokująco) */
        ustawWlascicielaPaleczki(druga_paleczka_id, id);

        jedz(id); /* Jedzenie */

        /* Odkładanie pałeczek (kolejność nieistotna, ale dla spójności odwrotna) */
        ustawWlascicielaPaleczki(druga_paleczka_id, -1);
        paleczki[druga_paleczka_id].unlock();
        ustawWlascicielaPaleczki(pierwsza_paleczka_id, -1);
        paleczki[pierwsza_paleczka_id].unlock();
    }
}

/**
 * @brief Główna funkcja programu.
 * Inicjalizuje środowisko, pyta użytkownika o tryb symulacji,
 * uruchamia wątki filozofów i główną pętlę wyświetlania stanu (ncurses),
 * czeka na zakończenie i wyświetla podsumowanie.
 */
/**
 * @brief Główna funkcja programu.
 * Inicjalizuje środowisko, pyta użytkownika o tryb symulacji,
 * uruchamia wątki filozofów i główną pętlę wyświetlania stanu (ncurses),
 * czeka na zakończenie i wyświetla podsumowanie.
 */
int main() {
    /* Menu wyboru logiki (wyświetlane w standardowej konsoli PRZED startem ncurses) */
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
            /* --- DODANE CZYSZCZENIE BUFORA --- */
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Ignoruj resztę linii, w tym Enter
            break; /* Poprawny wybór */
        }
        cout << "Niepoprawny wybor." << endl;
        /* Czyszczenie bufora cin na wypadek błędnego wejścia (np. tekstu) */
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    /* --- KONIEC ZMIANY --- */


    /* Inicjalizacja biblioteki ncurses */
    if (initscr() == NULL) { /* Start trybu ncurses */
        /* Jeśli inicjalizacja się nie powiodła, wypisz błąd i zakończ */
        fprintf(stderr, "Blad podczas inicjalizacji ncurses.\n");
        return 1;
    }
    noecho();               /* Nie wyświetlaj wciskanych klawiszy */
    cbreak();               /* Reaguj na klawisze natychmiast (bez buforowania linii) */
    nodelay(stdscr, TRUE);  /* Funkcja getch() nie będzie czekać na klawisz (tryb nieblokujący) */
    curs_set(0);            /* Ukryj kursor terminala */

    /* Inicjalizacja stanów początkowych filozofów i pałeczek oraz liczników */
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        stanyFilozofow[i] = StanFilozofa::MYSLI; /* Wszyscy zaczynają myśleć */
        wlascicielePaleczek[i] = -1; /* Wszystkie pałeczki są wolne */
        licznikPosilkow[i].store(0); /* Wyzeruj atomowe liczniki (bezpiecznie) */
    }

    /* Ustawianie zakresów czasu w zależności od wybranego trybu */
    switch (wyborLogiki) {
        case 1: /* Zakleszczenie */
            CZAS_MYSLENIA_MIN_MS = 100;
            CZAS_MYSLENIA_MAX_MS = 100;
            CZAS_JEDZENIA_MIN_MS = 2000;
            CZAS_JEDZENIA_MAX_MS = 60000;
            /* Upewnij się, że pauza w Zakleszczenie_Filozofowie jest AKTYWNA */
            // this_thread::sleep_for(chrono::milliseconds(100)); // <-- MUSI BYĆ AKTYWNE
            break;
        case 2: /* Zagłodzenie */
            CZAS_JEDZENIA_MIN_MS = 2000; CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000; CZAS_MYSLENIA_MAX_MS = 5000;
            break;
        case 3: /* Poprawny - Asymetria */
            CZAS_JEDZENIA_MIN_MS = 2000; CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000; CZAS_MYSLENIA_MAX_MS = 5000;
            break;
        case 4: /* Poprawny - Hierarchia */
            CZAS_JEDZENIA_MIN_MS = 2000; CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000; CZAS_MYSLENIA_MAX_MS = 5000;
            break;
    }

    /* Uruchamianie 5 wątków filozofów */
    thread watkiFilozofow[LICZBA_FILOZOFOW];
    /* Wątek wyświetlający został usunięty */

    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        /* Wybierz funkcję logiki na podstawie wyboru użytkownika */
        switch (wyborLogiki) {
            case 1: watkiFilozofow[i] = thread(Zakleszczenie_Filozofowie, i); break;
            case 2: watkiFilozofow[i] = thread(Zaglodzenie_Filozofowie, i); break;
            case 3: watkiFilozofow[i] = thread(Asymetria_Filozofowie, i); break;
            case 4: watkiFilozofow[i] = thread(Hierarchia_Filozofowie, i); break;
        }
    }

    /* --- Główna pętla programu (rysowanie stanu i obsługa wejścia w main) --- */
    /* Zmienne lokalne do przechowywania kopii stanu */
    vector<StanFilozofa> stany_kopia(LICZBA_FILOZOFOW);
    vector<int> liczniki_kopia(LICZBA_FILOZOFOW);
    vector<int> wlasciciele_kopia(LICZBA_FILOZOFOW);

    /* Pętla działa dopóki użytkownik nie naciśnie 'q' */
    while (symulacjaDziala) {
        /* Krok 1: Skopiuj aktualny stan globalny do zmiennych lokalnych (pod muteksem) */
        {
            lock_guard<mutex> blokada(mutexStanu);
            for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
                stany_kopia[i] = stanyFilozofow[i];
                liczniki_kopia[i] = licznikPosilkow[i].load();
                wlasciciele_kopia[i] = wlascicielePaleczek[i];
            }
        }

        /* Krok 2: Narysuj stan na ekranie ncurses */
        erase(); /* Wyczyść bufor ekranu ncurses */
        mvprintw(0, 0, "--- PROBLEM UCZTUJACYCH FILOZOFOW (NCURSES w main) ---");
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

        /* Krok 3: Sprawdź klawiaturę */
        int ch = getch(); /* Odczytaj znak z klawiatury (nie czeka) */

        /* --- DODANY DEBUG PRINT --- */
        // Tymczasowo pokaż, co odczytał getch() (jeśli nie ERR)
        if (ch != ERR) {
            // Rysuj w przedostatniej linii, żeby nie nadpisać "Nacisnij q"
            mvprintw(LINES - 2, 0, "Odczytano klawisz: %d ('%c')", ch, ch);
        } else {
            // Wyczyść poprzedni komunikat, jeśli nic nie naciśnięto
            mvprintw(LINES - 2, 0, "Odczytano klawisz: ERR     ");
        }
        /* --- KONIEC DEBUG PRINT --- */

        if (ch == 'q') {
            symulacjaDziala = false; /* Ustaw flagę zakończenia */
        }

        /* Krok 4: Pokaż wszystko na ekranie */
        refresh(); /* Aktualizuj fizyczny ekran terminala */

        /* Krok 5: Zaczekaj chwilę przed następnym odświeżeniem */
        napms(1000); /* Pauza na 100 milisekund */

    } /* Koniec głównej pętli while(symulacjaDziala) */


    /* Czekanie, aż wszystkie wątki filozofów zakończą swoją pracę */
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        watkiFilozofow[i].join(); /* Dołącz do wątku - czeka, aż wątek 'i' się zakończy */
    }

    /* Zakończenie pracy z biblioteką ncurses i przywrócenie normalnego terminala */
    curs_set(1);            /* Pokaż z powrotem kursor */
    nocbreak();             /* Wyłącz tryb cbreak */
    echo();                 /* Włącz z powrotem echo (wyświetlanie wciskanych klawiszy) */
    endwin();               /* Zakończ tryb ncurses */

    /* Wyświetlenie podsumowania w standardowej konsoli (po zamknięciu ncurses) */
    cout << "Symulacja zakonczona." << endl;
    cout << "\n--- OSTATECZNE PODSUMOWANIE POSILKOW ---" << endl;
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        cout << "  " << setw(10) << left << imionaFilozofow[i]
             << " (" << i << "): zjadl " << licznikPosilkow[i].load() << " razy." << endl;
    }

    return 0; /* Zakończ program pomyślnie */
} /* Koniec funkcji main */