#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <iomanip>
#include <limits>

using namespace std;

// --- Konfiguracja Symulacji (ZMODYFIKOWANE) ---
// Usunęliśmy 'const' i wartości początkowe.
// Ustawimy je w main() w zależności od wyboru użytkownika.
int CZAS_JEDZENIA_MIN_MS;
int CZAS_JEDZENIA_MAX_MS;
int CZAS_MYSLENIA_MIN_MS;
int CZAS_MYSLENIA_MAX_MS;

const int LICZBA_FILOZOFOW = 5;


// --- Pamięć Współdzielona ---
mutex paleczki[LICZBA_FILOZOFOW];

enum class StanFilozofa { MYSLI, GLODNY, JE };
StanFilozofa stanyFilozofow[LICZBA_FILOZOFOW];
int wlascicielePaleczek[LICZBA_FILOZOFOW];
string imionaFilozofow[LICZBA_FILOZOFOW] = { "Yoda", "Gandalf", "Platon", "Sokrates", "Konfucjusz" };

atomic<int> licznikPosilkow[LICZBA_FILOZOFOW];

mutex mutexStanu;
atomic<bool> symulacjaDziala{true};


// --- Funkcje pomocnicze (bez zmian) ---

int losujCzas(int min_ms, int max_ms) {
    thread_local mt19937 generator(random_device{}());
    // Jeśli min == max, dystrybucja i tak zwróci tę samą liczbę
    uniform_int_distribution<int> dystrybucja(min_ms, max_ms);
    return dystrybucja(generator);
}

string stanNaString(StanFilozofa stan) {
    switch (stan) {
        case StanFilozofa::MYSLI:   return "MYSLI";
        case StanFilozofa::GLODNY:  return "GLODNY";
        case StanFilozofa::JE:      return "JE";
    }
    return "???";
}

void ustawStanFilozofa(int id, StanFilozofa stan) {
    lock_guard<mutex> blokada(mutexStanu);
    stanyFilozofow[id] = stan;
}

void ustawWlascicielaPaleczki(int idPaleczki, int idFilozofa) {
    lock_guard<mutex> blokada(mutexStanu);
    wlascicielePaleczek[idPaleczki] = idFilozofa;
}

void mysl(int id) {
    ustawStanFilozofa(id, StanFilozofa::MYSLI);
    int czasMyslenia = losujCzas(CZAS_MYSLENIA_MIN_MS, CZAS_MYSLENIA_MAX_MS);
    this_thread::sleep_for(chrono::milliseconds(czasMyslenia));
}

void jedz(int id) {
    ustawStanFilozofa(id, StanFilozofa::JE);
    licznikPosilkow[id]++;
    int czasJedzenia = losujCzas(CZAS_JEDZENIA_MIN_MS, CZAS_JEDZENIA_MAX_MS);
    this_thread::sleep_for(chrono::milliseconds(czasJedzenia));
}


// --- Wątek wizualizatora (bez zmian) ---

void wyswietlStan() {
    while (symulacjaDziala) {
        cout << "\033[2J\033[H"; // Czyść ekran
        cout << "--- PROBLEM UCZTUJACYCH FILOZOFOW (C++) ---\n" << endl;

        {
            lock_guard<mutex> blokada(mutexStanu);

            cout << "FILOZOFOWIE:" << endl;
            for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
                cout << "  " << setw(10) << left << imionaFilozofow[i]
                     << " (" << i << "): "
                     << setw(7) << left << stanNaString(stanyFilozofow[i])
                     << " (Zjadl: " << licznikPosilkow[i].load() << ")" << endl;
            }

            cout << "\nPALECZKI:" << endl;
            for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
                cout << "  Paleczka " << i << ": ";
                if (wlascicielePaleczek[i] == -1) {
                    cout << "WOLNA" << endl;
                } else {
                    cout << "Trzymana przez " << imionaFilozofow[wlascicielePaleczek[i]]
                         << " (" << wlascicielePaleczek[i] << ")" << endl;
                }
            }
        }

        cout << "\nNacisnij ENTER, aby zakonczyc symulacje..." << endl;
        this_thread::sleep_for(chrono::seconds(1));
    }
}

// --- Logika filozofów (bez zmian) ---

void logikaFilozofa_Zakleszczenie(int id) {
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;
    while (symulacjaDziala) {
        mysl(id);
        ustawStanFilozofa(id, StanFilozofa::GLODNY);
        paleczki[lewa].lock();
        ustawWlascicielaPaleczki(lewa, id);
        // To opóźnienie jest kluczowe dla wymuszenia zakleszczenia
        this_thread::sleep_for(chrono::milliseconds(100));
        paleczki[prawa].lock();
        ustawWlascicielaPaleczki(prawa, id);
        jedz(id);
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock();
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock();
    }
}
void logikaFilozofa_Zaglodzenie(int id) {
    // 1. Ustalenie, które pałeczki są "moje"
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;

    // 2. Główna pętla życia (działa, dopóki program nie zostanie zatrzymany)
    while (symulacjaDziala) {

        // 3. ETAP MYŚLENIA
        //    Filozof myśli przez losowy czas (2-5 sekund).
        //    To ważne, bo "rozsynchronizowuje" filozofów.
        mysl(id);

        // 4. ETAP GŁODU
        //    Mówi Narratorowi (przez tablicę stanów), że jest głodny.
        ustawStanFilozofa(id, StanFilozofa::GLODNY);

        // 5. Ustawienie flagi: "Jeszcze nie zjadłem".
        bool zjadl = false;

        // 6. KRYTYCZNA PĘTLA "SPINOWANIA" (LIVELOCK)
        //    To jest serce problemu. Pętla ta oznacza:
        //    "DOPÓKI NIE ZJADŁEM I SYMULACJA DZIAŁA... PRÓBUJ!"
        while (!zjadl && symulacjaDziala) {

            // 7. PIERWSZA PRÓBA: Sięgnij po lewą pałeczkę
            //    `try_lock()` to funkcja "Spróbuj zamknąć".
            //    - Jeśli pałeczka jest WOLNA: Zamyka ją i zwraca `true`.
            //    - Jeśli pałeczka jest ZAJĘTA: NIE CZEKA. Zwraca `false`.
            if (paleczki[lewa].try_lock()) {

                // 7a. (Sukces z lewą) Zdobyliśmy lewą! Aktualizujemy tablicę wyników.
                ustawWlascicielaPaleczki(lewa, id);

                // 8. DRUGA PRÓBA: (Trzymając lewą) Sięgnij po prawą pałeczkę
                if (paleczki[prawa].try_lock()) {

                    // 8a. (Sukces z prawą) ZDOBYLIŚMY OBIE!
                    ustawWlascicielaPaleczki(prawa, id);

                    // 9. ETAP JEDZENIA
                    jedz(id); // Je przez losowy czas (i klika licznik)

                    // 10. WYJŚCIE Z PĘTLI
                    //     Ustawia flagę na `true`. Pętla 'while(!zjadl)'
                    //     zakończy się przy następnym sprawdzeniu.
                    zjadl = true;

                    // 11. ODKŁADANIE PAŁECZEK (po jedzeniu)
                    ustawWlascicielaPaleczki(prawa, -1);
                    paleczki[prawa].unlock();

                } else {
                    // 8b. (Porażka z prawą) Mamy lewą, ale prawa była zajęta.
                    //     Nie możemy jeść. Musimy odłożyć lewą.
                    ustawWlascicielaPaleczki(lewa, -1);
                }

                // 12. ODKŁADANIE LEWEJ
                //     Ta linia wykonuje się ZAWSZE po kroku 7a:
                //     - Albo po jedzeniu (krok 11)
                //     - Albo po porażce z prawą (krok 8b)
                paleczki[lewa].unlock();
            }

            // 13. CO SIĘ DZIEJE PO PORAŻCE? (NAJWAŻNIEJSZE!)
            //     - Co jeśli porażka była w kroku 7 (lewa była zajęta)?
            //     - Albo co jeśli porażka była w kroku 8b (prawa była zajęta)?
            //
            //     W obu tych przypadkach flaga `zjadl` wciąż jest `false`.
            //     Nie ma tu żadnej pauzy `sleep_for()`.
            //     Pętla 'while(!zjadl)' natychmiast się powtarza od kroku 6.
            //
            //     Filozof jest "uparty" - próbuje ponownie NATYCHMIAST.
            //     I znowu. I znowu. Tysiące razy na sekundę.
            //     To jest "spinowanie", które marnuje jego czas procesora.
        }

        // 14. KONIEC CYKLU
        //     Filozof wydostaje się z pętli 'while(!zjadl)' tylko wtedy,
        //     gdy udało mu się zjeść (krok 10).
        //     Teraz wraca na początek pętli życia (krok 2) i idzie myśleć.
    }
}

void logikaFilozofa_Poprawna(int id) {
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

// --- Funkcja `main` (ZMODYFIKOWANA) ---

int main() {
    // --- Inicjalizacja ---
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        stanyFilozofow[i] = StanFilozofa::MYSLI;
        wlascicielePaleczek[i] = -1;
        licznikPosilkow[i].store(0);
    }

    // --- Menu wyboru logiki ---
    int wyborLogiki = 0;
    cout << "Wybierz logike dzialania filozofow:" << endl;
    cout << "  1. Zakleszczenie (naiwna)" << endl;
    cout << "  2. Zaglodzenie (try_lock)" << endl;
    cout << "  3. Poprawna (asymetryczna)" << endl;

    while (true) {
        cout << "Twoj wybor (1, 2 lub 3): ";
        cin >> wyborLogiki;
        if (wyborLogiki >= 1 && wyborLogiki <= 3) {
            break;
        }
        cout << "Niepoprawny wybor." << endl;
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }

    // --- DODANO: Automatyczne ustawianie czasów ---

    switch (wyborLogiki) {
        case 1:
            // Tryb ZAKLESZCZENIA: krótki, stały czas myślenia
            cout << "Tryb 1 (Zakleszczenie): Ustawiono krótki, staly czas myslenia (100ms), aby wymusic zakleszczenie." << endl;
            CZAS_MYSLENIA_MIN_MS = 1000;
            CZAS_MYSLENIA_MAX_MS = 1000;
            CZAS_JEDZENIA_MIN_MS = 2000; // Długi czas, by zakleszczenie było widoczne
            CZAS_JEDZENIA_MAX_MS = 5000;
            break;

        case 2:
            // Tryb ZAGŁODZENIA: normalne, losowe czasy
            cout << "Tryb 2 (Zaglodzenie): Ustawiono normalne, losowe czasy. Obserwuj liczniki przez dluzszy czas." << endl;
            CZAS_JEDZENIA_MIN_MS = 1000;
            CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 5000;
            break;

        case 3:
            // Tryb POPRAWNY: normalne, losowe czasy (do porównania z trybem 2)
            cout << "Tryb 3 (Poprawny): Ustawiono normalne, losowe czasy. Obserwuj liczniki przez dluzszy czas." << endl;
            CZAS_JEDZENIA_MIN_MS = 1000;
            CZAS_JEDZENIA_MAX_MS = 4000;
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 5000;
            break;
    }


    cout << "\nUruchamianie symulacji... Nacisnij ENTER, aby zakonczyc." << endl;
    this_thread::sleep_for(chrono::seconds(2)); // Chwila na przeczytanie

    // --- Uruchomienie wątków ---

    thread watkiFilozofow[LICZBA_FILOZOFOW];
    thread watekWyswietlajacy(wyswietlStan);

    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        switch (wyborLogiki) {
            case 1:
                watkiFilozofow[i] = thread(logikaFilozofa_Zakleszczenie, i);
                break;
            case 2:
                watkiFilozofow[i] = thread(logikaFilozofa_Zaglodzenie, i);
                break;
            case 3:
                watkiFilozofow[i] = thread(logikaFilozofa_Poprawna, i);
                break;
        }
    }

    // --- Oczekiwanie na zakończenie ---
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cin.get();

    symulacjaDziala = false;

    // --- Sprzątanie ---
    cout << "Zatrzymywanie symulacji, prosze czekac..." << endl;

    watekWyswietlajacy.join();

    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        watkiFilozofow[i].join();
    }

    cout << "Symulacja zakonczona." << endl;

    // --- Podsumowanie końcowe ---
    cout << "\n--- OSTATECZNE PODSUMOWANIE POSILKOW ---" << endl;
    for (int i = 0; i < LICZBA_FILOZOFOW; ++i) {
        cout << "  " << setw(10) << left << imionaFilozofow[i]
             << " (" << i << "): zjadl " << licznikPosilkow[i].load() << " razy." << endl;
    }

    return 0;
}