// Mafia x Monopoly card game - console engine (C++17)
// Build: g++ -std=c++17 -O2 mafia_cards.cpp -o mafia_cards
//
// Data structures used:
//   Circular doubly linked list : seating / neighbours / elimination (Player::prev/next)
//   Stack (vector back)         : draw pile, discard pile
//   Hash map                    : each hand (card type -> count)
//   Queue-like vector           : pending kills, resolved at end of round
//   Hash set                    : doctor's protected players
//   Vector log                  : crime log used by police to arrest
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

enum Card { MONEY1, MONEY2, BRIBERY, ARREST, STEAL, BUY, SALE, PICK2, POWER, NCARDS };
const char* CARD_NAME[] = {"1 Money", "2 Money", "Bribery", "Arrest", "Steal from Neighbors",
                           "Buy", "Sale", "Pick 2", "Power Card"};
enum Character { MERCHANT, POLICE, BAKER, DRIVER };
const char* CHAR_NAME[] = {"Vegetable Merchant", "Police", "Baker", "Driver"};
enum Role { MAFIA, DOCTOR, DETECTIVE, JESTER, CIVILIAN };
const char* ROLE_NAME[] = {"Mafia", "Doctor", "Detective", "Jester", "Civilian"};

mt19937 rng(random_device{}());

struct Player {
    string name;
    Character ch = MERCHANT;
    Role role = CIVILIAN;
    unordered_map<int, int> hand;  // card type -> count (hidden from others)
    bool alive = true, skipNext = false;
    Player *prev = nullptr, *next = nullptr;  // circular doubly linked list
    int count() const { int c = 0; for (auto& kv : hand) c += kv.second; return c; }
    bool has(Card c) const { auto it = hand.find(c); return it != hand.end() && it->second > 0; }
    void add(Card c) { hand[c]++; }
    void remove(Card c) { if (--hand[c] <= 0) hand.erase(c); }
};

struct Crime {
    Player* who; bool bribe; int round;
    set<Player*> exemptFrom;  // police who may NOT arrest this crime (they were bribed)
};

// ---------- input helpers ----------
string readLine() {
    string s;
    if (!getline(cin, s)) { cout << "\n(input closed)\n"; exit(0); }
    return s;
}
int readInt(int lo, int hi) {
    while (true) {
        cout << "> ";
        string s = readLine();
        try {
            int v = stoi(s);
            if (v >= lo && v <= hi) return v;
        } catch (...) {}
        cout << "Enter a number from " << lo << " to " << hi << ".\n";
    }
}
void pause(const string& msg) { cout << msg << " (press Enter)"; readLine(); }
void clearScreen() { cout << string(60, '\n'); }

// ---------- game ----------
class Game {
    vector<unique_ptr<Player>> players;
    Player* head = nullptr;
    vector<Card> deck, discard;  // both used as stacks
    vector<Crime> crimes;
    vector<Player*> marked;      // pending kills
    set<Player*> saved;          // doctor protection this round
    int round = 0, banksInRow = 0;
    bool over = false;

    static int value(Card c, const Player* p) {
        switch (c) {
            case MONEY1: return 1;
            case MONEY2: return 2;
            case ARREST: return p->ch == POLICE ? 0 : 1;
            case SALE:   return p->ch == POLICE ? 2 : 0;
            default: return 0;
        }
    }
    int money(const Player* p) const {
        int t = 0;
        for (auto& kv : p->hand) t += value((Card)kv.first, p) * kv.second;
        return t;
    }
    int aliveCount() const { int n = 0; for (auto& p : players) n += p->alive; return n; }

    void say(const string& s) { cout << "  * " << s << "\n"; }

    void eliminate(Player* p, const string& why) {
        if (!p->alive) return;
        p->alive = false;
        for (auto& kv : p->hand) for (int i = 0; i < kv.second; i++) discard.push_back((Card)kv.first);
        p->hand.clear();
        if (p->next == p) head = nullptr;
        else {
            p->prev->next = p->next; p->next->prev = p->prev;
            if (head == p) head = p->next;
        }
        say(p->name + " (" + CHAR_NAME[p->ch] + ") is OUT: " + why);
    }
    void checkDebt(Player* p) {
        if (!p->alive) return;
        if (p->count() == 0 || (p->hand.size() == 1 && p->has(POWER)))
            eliminate(p, "in debt");
    }
    bool checkWin() {
        if (over) return true;
        int mafia = 0, civ = 0;
        for (auto& p : players) if (p->alive) (p->role == MAFIA ? mafia : civ)++;
        if (mafia == 0) { cout << "\n*** CIVILIANS WIN! ***\n"; over = true; }
        else if (civ == 0) { cout << "\n*** MAFIA WINS! ***\n"; over = true; }
        return over;
    }

    void draw(Player* p, int n) {
        for (int i = 0; i < n; i++) {
            if (deck.empty()) {
                if (discard.empty()) return;
                deck = discard; discard.clear();
                shuffle(deck.begin(), deck.end(), rng);
            }
            p->add(deck.back()); deck.pop_back();
        }
    }
    void discardCard(Player* p, Card c) { p->remove(c); discard.push_back(c); }

    // ---------- display ----------
    void showPublic() {
        cout << "\n=== ROUND " << round << " ===\nPlayers (seating order):\n";
        Player* c = head;
        if (c) do {
            cout << "  - " << c->name << " [" << CHAR_NAME[c->ch] << "]" << (c->skipNext ? " (arrested: skips next turn)" : "") << "\n";
            c = c->next;
        } while (c != head);
        cout << "Discard pile: ";
        int cnt[NCARDS] = {};
        for (Card d : discard) cnt[d]++;
        bool any = false;
        for (int i = 0; i < NCARDS; i++) if (cnt[i]) { cout << cnt[i] << "x " << CARD_NAME[i] << "  "; any = true; }
        if (!any) cout << "(empty)";
        cout << "\n";
    }
    void showHand(Player* p) {
        cout << "\n--- " << p->name << " | " << CHAR_NAME[p->ch] << " | secret role: " << ROLE_NAME[p->role] << " ---\nYour hand:\n";
        for (int i = 0; i < NCARDS; i++) if (p->has((Card)i)) cout << "  " << p->hand[i] << "x " << CARD_NAME[i] << "\n";
        cout << "Money value: " << money(p) << "\n";
    }

    // ---------- choosers ----------
    Player* chooseTarget(const string& prompt, const vector<Player*>& cands, bool allowCancel = false) {
        cout << prompt << "\n";
        for (size_t i = 0; i < cands.size(); i++)
            cout << "  " << i + 1 << ") " << cands[i]->name << " [" << CHAR_NAME[cands[i]->ch] << "]\n";
        if (allowCancel) cout << "  0) cancel\n";
        int v = readInt(allowCancel ? 0 : 1, (int)cands.size());
        return v == 0 ? nullptr : cands[v - 1];
    }
    Card chooseCard(Player* p, const string& prompt, function<bool(Card)> ok) {
        vector<Card> opts;
        for (int i = 0; i < NCARDS; i++) if (p->has((Card)i) && ok((Card)i)) opts.push_back((Card)i);
        cout << prompt << "\n";
        for (size_t i = 0; i < opts.size(); i++) cout << "  " << i + 1 << ") " << CARD_NAME[opts[i]] << "\n";
        return opts[readInt(1, (int)opts.size()) - 1];
    }
    vector<Player*> others(Player* me, function<bool(Player*)> ok = [](Player*) { return true; }) {
        vector<Player*> v;
        Player* c = me->next;
        while (c != me) { if (ok(c)) v.push_back(c); c = c->next; }
        return v;
    }

    // victim privately picks a card to hand over; if none, victim is out
    void takeCardFrom(Player* taker, Player* victim) {
        if (victim->count() == 0) { eliminate(victim, "had nothing to give"); return; }
        pause("Pass the device to " + victim->name + " (everyone else look away)");
        clearScreen();
        showHand(victim);
        Card c = chooseCard(victim, victim->name + ", choose a card to give to " + taker->name + ":", [](Card) { return true; });
        victim->remove(c); taker->add(c);
        clearScreen();
        pause("Pass the device back to " + taker->name);
        say(taker->name + " took a card from " + victim->name + ".");
        checkDebt(victim);
    }

    // ---------- actions ----------
    void doSteal(Player* p) {
        discardCard(p, STEAL);
        crimes.push_back({p, false, round, {}});
        vector<Player*> victims = {p->prev};
        if (p->next != p->prev) victims.push_back(p->next);
        for (Player* v : victims) {
            if (v == p || !v->alive) continue;
            Card best = NCARDS; int bv = 99;
            for (int i = 0; i < NCARDS; i++)
                if (v->has((Card)i) && value((Card)i, v) > 0 && value((Card)i, v) < bv) { bv = value((Card)i, v); best = (Card)i; }
            if (best == NCARDS) eliminate(v, "robbed with no money (debt)");
            else {
                v->remove(best); p->add(best);
                say(p->name + " stole 1 money from " + v->name + ".");
                checkDebt(v);
            }
        }
    }
    void doBribe(Player* p) {
        auto police = others(p, [](Player* x) { return x->ch == POLICE; });
        Player* cop = chooseTarget("Bribe which police?", police, true);
        if (!cop) return;
        discardCard(p, BRIBERY);
        crimes.push_back({p, true, round, {}});
        for (auto& c : crimes) if (c.who == p) c.exemptFrom.insert(cop);
        say(p->name + " bribed " + cop->name + " (bribery is itself a crime for other police).");
    }
    vector<Player*> arrestable(Player* cop) {
        vector<Player*> v;
        for (auto& c : crimes)
            if (c.who->alive && c.who != cop && c.round >= round - 1 && !c.exemptFrom.count(cop) &&
                find(v.begin(), v.end(), c.who) == v.end())
                v.push_back(c.who);
        return v;
    }
    void doArrest(Player* p) {
        Player* t = chooseTarget("Arrest whom?", arrestable(p), true);
        if (!t) return;
        discardCard(p, ARREST);
        crimes.erase(remove_if(crimes.begin(), crimes.end(), [&](const Crime& c) { return c.who == t; }), crimes.end());
        say(p->name + " arrested " + t->name + "!");
        if (t->role == JESTER) {
            cout << "\n*** THE JESTER (" << t->name << ") WAS ARRESTED - JESTER WINS! ***\n";
            over = true;
        } else t->skipNext = true;
    }
    void doBuy(Player* p) {
        auto ts = others(p, [](Player* x) { return x->ch == MERCHANT || x->ch == BAKER || x->ch == DRIVER; });
        Player* t = chooseTarget("Take a card from:", ts, true);
        if (!t) return;
        discardCard(p, BUY);
        takeCardFrom(p, t);
    }
    void doSale(Player* p) {
        auto pool = others(p);
        vector<Player*> picked;
        discardCard(p, SALE);
        for (int i = 0; i < 3 && !pool.empty(); i++) {
            Player* t = chooseTarget("Pick a player to take a card from (" + to_string(i + 1) + "/3):", pool, i > 0);
            if (!t) break;
            picked.push_back(t);
            pool.erase(find(pool.begin(), pool.end(), t));
        }
        for (Player* t : picked) if (t->alive) takeCardFrom(p, t);
    }
    void buyPower(Player* p) {
        int need = 4;
        while (need > 0) {
            cout << "Still need " << need << " money. ";
            Card c = chooseCard(p, "Pay with:", [&](Card x) { return value(x, p) > 0; });
            need -= value(c, p);
            discardCard(p, c);
        }
        p->add(POWER);
        say(p->name + " bought a power card.");
    }
    void usePower(Player* p) {
        discardCard(p, POWER);
        vector<Player*> all;
        for (auto& x : players) if (x->alive) all.push_back(x.get());
        if (p->role == MAFIA) {
            auto ts = others(p);
            marked.push_back(chooseTarget("Kill whom?", ts));
        } else if (p->role == DOCTOR) {
            saved.insert(chooseTarget("Save whom (you may pick yourself)?", all));
        } else if (p->role == DETECTIVE) {
            Player* t = chooseTarget("Investigate whom?", others(p));
            cout << "  >> " << t->name << " is secretly: " << ROLE_NAME[t->role] << "\n";
            pause("Memorize it, then continue");
            clearScreen();
        }
        say(p->name + " used a power card.");
    }

    void playTurn(Player* p) {
        clearScreen();
        pause("Pass the device to " + p->name);
        clearScreen();
        showPublic();
        showHand(p);

        vector<pair<string, int>> acts;  // label, code (card id, 100 = buy power, 101 = bank)
        if (p->has(STEAL) && aliveCount() >= 2) acts.push_back({"Play Steal from Neighbors", STEAL});
        if (p->has(BRIBERY) && !others(p, [](Player* x) { return x->ch == POLICE; }).empty()) acts.push_back({"Play Bribery", BRIBERY});
        if (p->has(ARREST) && p->ch == POLICE && !arrestable(p).empty()) acts.push_back({"Play Arrest", ARREST});
        if (p->has(BUY) && !others(p, [](Player* x) { return x->ch != POLICE; }).empty()) acts.push_back({"Play Buy", BUY});
        if (p->has(SALE) && p->ch != POLICE) acts.push_back({"Play Sale", SALE});
        if (p->has(PICK2)) acts.push_back({"Play Pick 2", PICK2});
        if (p->has(POWER) && p->role != CIVILIAN && p->role != JESTER) acts.push_back({"Use Power Card", POWER});
        if (money(p) >= 4 && (p->role == MAFIA || p->role == DOCTOR || p->role == DETECTIVE))
            acts.push_back({"Buy Power Card (pay 4 money)", 100});
        if (acts.empty()) acts.push_back({"No playable action: keep cards and end turn", 101});

        cout << "\nChoose an action:\n";
        for (size_t i = 0; i < acts.size(); i++) cout << "  " << i + 1 << ") " << acts[i].first << "\n";
        int code = acts[readInt(1, (int)acts.size()) - 1].second;
        banksInRow = (code == 101) ? banksInRow + 1 : 0;

        switch (code) {
            case STEAL: doSteal(p); break;
            case BRIBERY: doBribe(p); break;
            case ARREST: doArrest(p); break;
            case BUY: doBuy(p); break;
            case SALE: doSale(p); break;
            case PICK2: discardCard(p, PICK2); draw(p, 2); say(p->name + " picked 2 cards."); break;
            case POWER: usePower(p); break;
            case 100: buyPower(p); break;
            default: break;
        }
        checkDebt(p);
        checkWin();
        if (!over) { pause("Turn over, hide your hand"); clearScreen(); }
    }

    void endRound() {
        cout << "\n--- End of round " << round << " ---\n";
        for (Player* t : marked) {
            if (!t->alive) continue;
            if (saved.count(t)) say("A kill attempt failed: the target was saved.");
            else eliminate(t, "killed by the mafia");
        }
        marked.clear(); saved.clear();
        crimes.erase(remove_if(crimes.begin(), crimes.end(), [&](const Crime& c) { return c.round < round - 1 || !c.who->alive; }), crimes.end());
        checkWin();
    }

public:
    void setup() {
        cout << "=== MAFIA x MONOPOLY ===\nNumber of players (4-20): ";
        int n = readInt(4, 20);
        for (int i = 0; i < n; i++) {
            auto p = make_unique<Player>();
            cout << "Name of player " << i + 1 << ": ";
            p->name = readLine();
            if (p->name.empty()) p->name = "P" + to_string(i + 1);
            players.push_back(move(p));
        }
        // suggested seating = shuffled order, linked into a circular list
        shuffle(players.begin(), players.end(), rng);
        for (int i = 0; i < n; i++) {
            players[i]->next = players[(i + 1) % n].get();
            players[i]->prev = players[(i + n - 1) % n].get();
        }
        head = players[0].get();
        cout << "\nSuggested seating (clockwise, last sits next to first):\n";
        for (auto& p : players) cout << "  " << p->name << "\n";

        // roles scale with player count
        vector<Role> roles = {DOCTOR, DETECTIVE};
        int mafia = max(1, n / 4);
        for (int i = 0; i < mafia; i++) roles.push_back(MAFIA);
        if (n >= 5) roles.push_back(JESTER);
        while ((int)roles.size() < n) roles.push_back(CIVILIAN);
        shuffle(roles.begin(), roles.end(), rng);

        // deck scales per player; Pick 2 is most common
        int copies[NCARDS] = {2, 1, 1, 1, 1, 1, 1, 3, 0};
        for (int i = 0; i < n; i++)
            for (int c = 0; c < NCARDS; c++) for (int k = 0; k < copies[c]; k++) deck.push_back((Card)c);
        shuffle(deck.begin(), deck.end(), rng);

        uniform_int_distribution<int> pick(0, 3);
        for (int i = 0; i < n; i++) {
            players[i]->role = roles[i];
            players[i]->ch = (Character)pick(rng);  // repeats allowed
            draw(players[i].get(), 3);
        }
        cout << "\nCharacters (public):\n";
        for (auto& p : players) cout << "  " << p->name << " - " << CHAR_NAME[p->ch] << "\n";
        for (auto& p : players) {
            pause("\nPass the device to " + p->name + " to see their secret role");
            clearScreen(); showHand(p.get());
            pause("Memorize it"); clearScreen();
        }
        for (auto& p : players) checkDebt(p.get());
    }

    void run() {
        while (!over) {
            round++;
            vector<Player*> order;
            if (head) { Player* c = head; do { order.push_back(c); c = c->next; } while (c != head); }
            for (Player* p : order) {
                if (over) break;
                if (!p->alive) continue;
                if (p->skipNext) { p->skipNext = false; say(p->name + " is under arrest and skips this turn."); continue; }
                playTurn(p);
            }
            if (!over) endRound();
            if (!over && banksInRow >= 2 * aliveCount()) {
                cout << "\nEveryone is stuck with no playable cards. DRAW.\n";
                over = true;
            }
        }
        cout << "\nFinal roles:\n";
        for (auto& p : players) cout << "  " << p->name << ": " << ROLE_NAME[p->role] << (p->alive ? "" : " (out)") << "\n";
    }
};

int main() {
    Game g;
    g.setup();
    g.run();
    return 0;
}