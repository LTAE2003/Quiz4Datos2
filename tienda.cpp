// Tienda en línea con Redis (base de datos NoSQL de tipo llave-valor) en C++.
//
// Escenario: una tienda necesita guardar, de forma muy rápida:
//   1. Sesiones de usuario que expiran solas (SET con TTL).
//   2. El carrito de compras temporal de cada usuario (HASH).
//   3. Un contador de visitas por producto (INCR).
//   4. Un ranking de los productos más vistos (SORTED SET).
//
// Uso:
//   ./tienda          menú interactivo
//   ./tienda --demo   recorrido automático de ejemplo

#include <hiredis/hiredis.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int SESSION_TTL = 60;  // segundos que dura una sesión sin actividad

struct Producto {
    string nombre;
    long precio;
};

// Catálogo de ejemplo (en una app real vendría de otra base de datos)
const map<string, Producto> CATALOGO = {
    {"p1", {"Teclado", 25000}},
    {"p2", {"Mouse", 12000}},
    {"p3", {"Monitor", 95000}},
    {"p4", {"Audifonos", 30000}},
};

// Libera automáticamente las respuestas de hiredis
struct ReplyDeleter {
    void operator()(redisReply* r) const { freeReplyObject(r); }
};
using Reply = unique_ptr<redisReply, ReplyDeleter>;

class Tienda {
public:
    // Crea la conexión a Redis usando variables de entorno (o valores por defecto)
    Tienda() {
        const char* h = getenv("REDIS_HOST");
        const char* p = getenv("REDIS_PORT");
        string host = h ? h : "127.0.0.1";
        int port = p ? atoi(p) : 6379;
        ctx_ = redisConnect(host.c_str(), port);
        if (ctx_ == nullptr || ctx_->err) {
            string msg = ctx_ ? ctx_->errstr : "sin memoria";
            if (ctx_) redisFree(ctx_);
            throw runtime_error("No se pudo conectar a Redis: " + msg);
        }
    }
    ~Tienda() { redisFree(ctx_); }
    Tienda(const Tienda&) = delete;
    Tienda& operator=(const Tienda&) = delete;

    // ---------- 1. Sesiones: llave -> valor con expiración ----------
    string iniciarSesion(const string& usuario) {
        string token = generarToken();
        // SET llave valor EX segundos
        cmd("SET sesion:%s %s EX %d", token.c_str(), usuario.c_str(), SESSION_TTL);
        return token;
    }

    // Devuelve el usuario dueño de la sesión o "" si expiró
    string usuarioDeSesion(const string& token) {
        Reply r = cmd("GET sesion:%s", token.c_str());
        if (r->type != REDIS_REPLY_STRING) return "";
        cmd("EXPIRE sesion:%s %d", token.c_str(), SESSION_TTL);  // renueva el tiempo
        return r->str;
    }

    long ttlSesion(const string& token) {
        return cmd("TTL sesion:%s", token.c_str())->integer;
    }

    void cerrarSesion(const string& token) { cmd("DEL sesion:%s", token.c_str()); }

    // ---------- 2. Carrito: llave -> hash (producto -> cantidad) ----------
    void agregarAlCarrito(const string& usuario, const string& productoId, int cantidad = 1) {
        if (!CATALOGO.count(productoId))
            throw invalid_argument("Producto desconocido: " + productoId);
        cmd("HINCRBY carrito:%s %s %d", usuario.c_str(), productoId.c_str(), cantidad);
        cmd("EXPIRE carrito:%s 3600", usuario.c_str());  // el carrito se borra tras 1 hora
    }

    void mostrarCarrito(const string& usuario) {
        Reply r = cmd("HGETALL carrito:%s", usuario.c_str());
        if (r->type != REDIS_REPLY_ARRAY || r->elements == 0) {
            cout << "El carrito esta vacio.\n";
            return;
        }
        long total = 0;
        // HGETALL devuelve [campo1, valor1, campo2, valor2, ...]
        for (size_t i = 0; i + 1 < r->elements; i += 2) {
            string pid = r->element[i]->str;
            int cant = atoi(r->element[i + 1]->str);
            const Producto& p = CATALOGO.at(pid);
            long subtotal = p.precio * cant;
            total += subtotal;
            cout << "  " << p.nombre << " x" << cant << " = " << subtotal << "\n";
        }
        cout << "  TOTAL: " << total << "\n";
    }

    // ---------- 3 y 4. Contador de visitas y ranking ----------
    void registrarVisita(const string& productoId) {
        cmd("INCR visitas:%s", productoId.c_str());                 // contador atómico
        cmd("ZINCRBY ranking:visitas 1 %s", productoId.c_str());    // sorted set para el top
    }

    void mostrarTop(int n = 3) {
        Reply r = cmd("ZREVRANGE ranking:visitas 0 %d WITHSCORES", n - 1);
        if (r->type != REDIS_REPLY_ARRAY || r->elements == 0) {
            cout << "  (aun no hay visitas)\n";
            return;
        }
        // Devuelve [miembro1, puntaje1, miembro2, puntaje2, ...]
        for (size_t i = 0; i + 1 < r->elements; i += 2) {
            string pid = r->element[i]->str;
            int visitas = atoi(r->element[i + 1]->str);
            cout << "  " << CATALOGO.at(pid).nombre << ": " << visitas << " visitas\n";
        }
    }

    void limpiar() { cmd("FLUSHDB"); }

private:
    redisContext* ctx_ = nullptr;

    // Ejecuta un comando. Los %s se envían como argumentos seguros (no se concatenan).
    template <typename... Args>
    Reply cmd(const char* formato, Args... args) {
        Reply r(static_cast<redisReply*>(redisCommand(ctx_, formato, args...)));
        if (!r) throw runtime_error(string("Error de conexion con Redis: ") + ctx_->errstr);
        if (r->type == REDIS_REPLY_ERROR) throw runtime_error(string("Error de Redis: ") + r->str);
        return r;
    }

    static string generarToken() {
        static mt19937 gen{random_device{}()};
        static const char* hex = "0123456789abcdef";
        string t;
        for (int i = 0; i < 8; i++) t += hex[gen() % 16];
        return t;
    }
};

void demo(Tienda& t) {
    cout << "== Demo automatica ==\n";
    string token = t.iniciarSesion("ana");
    cout << "Sesion iniciada para ana (token " << token << ", expira en " << SESSION_TTL << "s)\n";
    cout << "Usuario de la sesion: " << t.usuarioDeSesion(token) << "\n";
    cout << "TTL restante: " << t.ttlSesion(token) << " s\n";

    for (string pid : {"p1", "p3", "p3", "p2", "p3", "p1"}) t.registrarVisita(pid);
    t.agregarAlCarrito("ana", "p1", 1);
    t.agregarAlCarrito("ana", "p2", 2);
    t.agregarAlCarrito("ana", "p1", 1);  // suma a la cantidad existente

    cout << "\nCarrito de ana:\n";
    t.mostrarCarrito("ana");
    cout << "\nTop productos mas vistos:\n";
    t.mostrarTop();

    t.cerrarSesion(token);
    string u = t.usuarioDeSesion(token);
    cout << "\nSesion cerrada. Sigue activa? " << (u.empty() ? "No" : "Si") << "\n";
}

void menu(Tienda& t) {
    string token, linea;
    while (true) {
        cout << "\n1) Iniciar sesion  2) Ver catalogo  3) Agregar al carrito\n"
             << "4) Ver carrito     5) Top mas vistos 6) Cerrar sesion  0) Salir\nOpcion: ";
        if (!getline(cin, linea)) break;
        if (linea == "0") break;
        if (linea == "1") {
            cout << "Nombre de usuario: ";
            string usuario;
            getline(cin, usuario);
            token = t.iniciarSesion(usuario);
            cout << "Sesion iniciada (token " << token << ").\n";
        } else if (linea == "2") {
            for (const auto& [pid, p] : CATALOGO)
                cout << "  " << pid << ": " << p.nombre << " - " << p.precio << "\n";
        } else if (linea == "5") {
            t.mostrarTop();
        } else if (linea == "3" || linea == "4" || linea == "6") {
            // Estas opciones requieren una sesión válida
            string usuario = token.empty() ? "" : t.usuarioDeSesion(token);
            if (usuario.empty()) {
                cout << "Necesita iniciar sesion (o su sesion expiro).\n";
                continue;
            }
            if (linea == "3") {
                cout << "ID de producto (p1-p4): ";
                string pid;
                getline(cin, pid);
                try {
                    t.agregarAlCarrito(usuario, pid);
                    t.registrarVisita(pid);
                    cout << "Agregado.\n";
                } catch (const invalid_argument& e) {
                    cout << e.what() << "\n";
                }
            } else if (linea == "4") {
                t.mostrarCarrito(usuario);
            } else {
                t.cerrarSesion(token);
                token.clear();
                cout << "Sesion cerrada.\n";
            }
        }
    }
}

int main(int argc, char** argv) {
    try {
        Tienda tienda;
        if (argc > 1 && string(argv[1]) == "--demo")
            demo(tienda);
        else
            menu(tienda);
    } catch (const exception& e) {
        cerr << e.what() << "\n¿Ejecutó 'docker compose up -d'?\n";
        return 1;
    }
    return 0;
}
