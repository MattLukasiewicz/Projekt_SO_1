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


/*
 * Deklaruje zmineen żeby móc zmieniać czas dla róznych symulacji ich czas będzie zależał od opcji która wypbierzemy
 */
int CZAS_JEDZENIA_MIN_MS;
int CZAS_JEDZENIA_MAX_MS;
int CZAS_MYSLENIA_MIN_MS;
int CZAS_MYSLENIA_MAX_MS;

const int LICZBA_FILOZOFOW = 5;


//Pamięć Współdzielona mutex to klucz do pokoju i tylko jedna osoba może go użyć i wejść tam fajna analogia

mutex paleczki[LICZBA_FILOZOFOW];

enum class StanFilozofa { MYSLI, GLODNY, JE };//dla ładniejszego odczytu
StanFilozofa stanyFilozofow[LICZBA_FILOZOFOW];// do wyswietlania
int wlascicielePaleczek[LICZBA_FILOZOFOW];// do wyswietlania -`1 to wolna paleczka
string imionaFilozofow[LICZBA_FILOZOFOW] = { "Shrek", "Fiona", "Osioł", "Kot", "Smoczyca" };

atomic<int> licznikPosilkow[LICZBA_FILOZOFOW];

mutex mutexStanu;//żeby przy wyswitalniu czy zapisie nie było oknfliktuu że coś w rtrakcie sie zminia
atomic<bool> symulacjaDziala{true};


// FUNKCJ POMOCNICZE


/*
 * Losuje czas z zakresu podanego wcześńiej
 */
int losujCzas(int min_ms, int max_ms) {
    thread_local mt19937 generator(random_device{}());
    // Jeśli min == max, dystrybucja i tak zwróci tę samą liczbę
    uniform_int_distribution<int> dystrybucja(min_ms, max_ms);
    return dystrybucja(generator);
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
    lock_guard<mutex> blokada(mutexStanu);
    stanyFilozofow[id] = stan;
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
    licznikPosilkow[id]++;
    int czasJedzenia = losujCzas(CZAS_JEDZENIA_MIN_MS, CZAS_JEDZENIA_MAX_MS);
    this_thread::sleep_for(chrono::milliseconds(czasJedzenia));
}


// --- Wątek wizualizatora (bez zmian) ---

void wyswietlStan() {
    while (symulacjaDziala) {
        //cout << "\033[2J\033[H"; // Czyść ekran
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
    //Działanie symulacji dopóki w main nie będzie false czyli przycisk enter
    while (symulacjaDziala) {
        //ustawaimy myslenie i filozof myśli przez jakis czas
        mysl(id);
        //filozof jest głodny
        ustawStanFilozofa(id, StanFilozofa::GLODNY);
        //paleczka leewa jest pobierana pierwsza jeśli to możliwe i blokuuje ja a jesli nie to czeka aż będzie wolna
        paleczki[lewa].lock();
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
        jedz(id);
        // oddanie pałeczek
        ustawWlascicielaPaleczki(prawa, -1);
        paleczki[prawa].unlock();
        ustawWlascicielaPaleczki(lewa, -1);
        paleczki[lewa].unlock();
    }
}

/*
 * Używamy tu try_lock() żeby spróbować podnieść pałeczkę jeśli się nie uuda
 * to próbuje ponownie  co może powodować że jeden filozof będzie całyc zas
 * wyprzedzany w podnoszeniu pałeczek przez inych filozofów i bdzie mniej jadł.
 *
 */


void Zaglodzenie_Filozofowie(int id) {
    // jakei pałeczki podnosi jak np id =4 to paleczka 4 i 5 mod 5 czyli 0
    int lewa = id;
    int prawa = (id + 1) % LICZBA_FILOZOFOW;

    while (symulacjaDziala) {

        //  ETAP MYŚLENIA
        //    Filozof myśli przez losowy czas (2-5 sekund) aby ich rozsynchornizować
        mysl(id);

        //   Jest głodny
        ustawStanFilozofa(id, StanFilozofa::GLODNY);

        // Ustawaimy flagę czy już zjadł czy nie  jeśli nie to cały czas próbuje jesc dopóki zjadł = true albo symulacja się nie skończy
        bool zjadl = false;

        /*
         * Pętla "spinująca"
         * Serce mechanizmu livelocka. Pętla wykonuje się bardzo szybko ,
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


            //     Filozof jest "uparty" - próbuje ponownie NATYCHMIAST.
            //     I znowu. I znowu. Tysiące razy na sekundę.
            //     To jest "spinowanie", które marnuje jego czas procesora.
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
            CZAS_MYSLENIA_MIN_MS = 2000;
            CZAS_MYSLENIA_MAX_MS = 2000;
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
                watkiFilozofow[i] = thread(Zakleszczenie_Filozofowie, i);
                break;
            case 2:
                watkiFilozofow[i] = thread(Zaglodzenie_Filozofowie, i);
                break;
            case 3:
                watkiFilozofow[i] = thread(Asymetria_Filozofowie, i);
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