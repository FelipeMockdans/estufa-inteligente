import sqlite3, time
from flask import Flask, request, jsonify, render_template

app = Flask(__name__)
DB = "estufa.db"
ACOES = ("ligar", "desligar", "auto")

def conectar():
    c = sqlite3.connect(DB)
    c.row_factory = sqlite3.Row
    return c

def iniciar_banco():
    with conectar() as c:
        c.execute("""CREATE TABLE IF NOT EXISTS leituras (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ts INTEGER,
            solo_pct INTEGER,
            umidade_ar REAL,
            temperatura REAL,
            luminosidade_pct REAL,
            bomba INTEGER,
            luminaria INTEGER)""")
        c.execute("CREATE TABLE IF NOT EXISTS estado (chave TEXT PRIMARY KEY, valor TEXT)")
        c.execute("INSERT OR IGNORE INTO estado VALUES ('cmd_bomba', 'auto')")
        c.execute("INSERT OR IGNORE INTO estado VALUES ('cmd_luz', 'auto')")

def ler_comandos():
    with conectar() as c:
        return {r["chave"]: r["valor"] for r in c.execute("SELECT * FROM estado")}

@app.post("/api/leituras")
def receber():
    d = request.get_json(silent=True)
    if not d or "solo_pct" not in d:
        return jsonify(erro="JSON invalido: falta solo_pct"), 400
    with conectar() as c:
        c.execute("""INSERT INTO leituras
                     (ts, solo_pct, umidade_ar, temperatura, luminosidade_pct, bomba, luminaria)
                     VALUES (?,?,?,?,?,?,?)""",
                  (int(time.time()), d.get("solo_pct"), d.get("umidade_ar"),
                   d.get("temperatura"), d.get("luminosidade_pct"),
                   int(bool(d.get("bomba"))), int(bool(d.get("luminaria")))))
    # a resposta leva os comandos para o ESP32
    return jsonify(ok=True, **ler_comandos())

@app.get("/api/status")
def status():
    with conectar() as c:
        r = c.execute("SELECT * FROM leituras ORDER BY id DESC LIMIT 1").fetchone()
    if not r:
        return jsonify(erro="sem leituras ainda"), 404
    d = dict(r)
    d.update(ler_comandos())
    return jsonify(d)

@app.get("/api/historico")
def historico():
    horas = request.args.get("horas", 1, type=float)
    desde = int(time.time() - horas * 3600)
    with conectar() as c:
        rows = c.execute("""SELECT ts, solo_pct, umidade_ar, temperatura,
                            luminosidade_pct, bomba, luminaria
                            FROM leituras WHERE ts >= ? ORDER BY ts""", (desde,)).fetchall()
    return jsonify([dict(r) for r in rows])

def definir(chave):
    d = request.get_json(silent=True) or {}
    acao = d.get("acao") or request.form.get("acao")
    if acao not in ACOES:
        return jsonify(erro="acao invalida: use ligar, desligar ou auto"), 400
    with conectar() as c:
        c.execute("INSERT OR REPLACE INTO estado VALUES (?, ?)", (chave, acao))
    return jsonify({chave: acao})

@app.route("/api/bomba", methods=["GET", "POST"])
def bomba():
    if request.method == "POST":
        return definir("cmd_bomba")
    return jsonify(cmd_bomba=ler_comandos()["cmd_bomba"])

@app.route("/api/luz", methods=["GET", "POST"])
def luz():
    if request.method == "POST":
        return definir("cmd_luz")
    return jsonify(cmd_luz=ler_comandos()["cmd_luz"])

@app.get("/")
def dashboard():
    return render_template("index.html")

if __name__ == "__main__":
    iniciar_banco()
    app.run(host="0.0.0.0", port=5000)