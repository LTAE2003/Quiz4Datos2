# Quiz4Datos2

Programa en C++ que usa **Redis**, una base de datos NoSQL de tipo **llave-valor**, desde código mediante la librería cliente **hiredis**.

## Contexto del problema

Una tienda en línea necesita responder muy rápido a varias tareas que no requieren consultas complejas, sino guardar y leer datos por una llave:

1. **Sesiones de usuario:** cuando alguien inicia sesión se le asigna un token, y la sesión debe **expirar sola** tras un tiempo sin actividad.
2. **Carrito de compras:** cada usuario tiene un carrito temporal con productos y cantidades.
3. **Visitas por producto:** hay que contar las visitas de forma segura aunque muchos usuarios entren al mismo tiempo.
4. **Ranking:** se quiere mostrar los productos más vistos.

## Por qué una base llave-valor

En una base llave-valor se guarda un dato y se recupera usando su llave, como un diccionario gigante. Eso encaja con este problema porque:

- Todas las lecturas se hacen por una llave conocida (el token, el usuario o el producto), sin necesitar joins ni consultas complejas.
- Redis trabaja en memoria, así que las lecturas y escrituras son muy rápidas.
- Soporta **expiración automática (TTL)** de las llaves, ideal para sesiones y carritos temporales.
- Sus operaciones, como `INCR`, son atómicas, lo que evita errores al contar con varios usuarios a la vez.

## Cómo se usa Redis en el código

La clase `Tienda` en `tienda.cpp` guarda la conexión (`redisContext`) y envía los comandos con `redisCommand`. Las respuestas se envuelven en un `unique_ptr` para liberar la memoria automáticamente.

| Necesidad | Estructura de Redis | Llave de ejemplo | Comandos usados |
|---|---|---|---|
| Sesión con expiración | String con TTL | `sesion:a1b2c3d4` -> `ana` | `SET ... EX`, `GET`, `EXPIRE`, `TTL`, `DEL` |
| Carrito de compras | Hash | `carrito:ana` -> `{p1: 2, p2: 1}` | `HINCRBY`, `HGETALL`, `EXPIRE` |
| Contador de visitas | String numérico | `visitas:p3` -> `3` | `INCR` |
| Ranking de más vistos | Sorted set | `ranking:visitas` | `ZINCRBY`, `ZREVRANGE` |

Las llaves usan el patrón `tipo:identificador` (por ejemplo `carrito:ana`), una convención común en Redis para organizar los datos.

## Requisitos

- Compilador de C++ con soporte de C++17 (g++ o clang++)
- Librería **hiredis** (cliente de Redis para C)
- Docker (para levantar Redis fácilmente), o un Redis instalado localmente

Instalación de hiredis:

```bash
# Ubuntu / Debian
sudo apt install libhiredis-dev

# macOS (Homebrew)
brew install hiredis
```

En macOS con Homebrew, si el compilador no encuentra la librería, compile con:
`make CXXFLAGS="-std=c++17 -I$(brew --prefix)/include" LDLIBS="-L$(brew --prefix)/lib -lhiredis"`

## Cómo ejecutarlo

1. Levantar Redis:

   ```bash
   docker compose up -d
   ```

2. Compilar:

   ```bash
   make
   ```

3. Ejecutar:

   ```bash
   ./tienda --demo   # recorrido automático de ejemplo
   ./tienda          # menú interactivo
   ```

Si Redis corre en otro host o puerto, se pueden usar las variables de entorno `REDIS_HOST` y `REDIS_PORT`.

## Ejemplo de salida (`--demo`)

```
== Demo automatica ==
Sesion iniciada para ana (token 42724ca3, expira en 60s)
Usuario de la sesion: ana
TTL restante: 60 s

Carrito de ana:
  Teclado x2 = 50000
  Mouse x2 = 24000
  TOTAL: 74000

Top productos mas vistos:
  Monitor: 3 visitas
  Teclado: 2 visitas
  Mouse: 1 visitas

Sesion cerrada. Sigue activa? No
```

## Estructura del repositorio

- `tienda.cpp`: programa principal con la clase que usa Redis y la interfaz de consola.
- `Makefile`: compila el programa con `make`.
- `docker-compose.yml`: levanta un servidor Redis local.
- `README.md`: este documento.

## Limitaciones

- El catálogo está escrito en el código; en una aplicación real vendría de otra base de datos.
- Redis guarda los datos en memoria, por lo que no es la mejor opción para datos que no se pueden perder (como el historial de pedidos). Para eso se usaría una base de datos persistente junto con Redis.
