# 🌱 Estufa Inteligente – Monitoramento e Automação IoT

## Sobre o projeto

A Estufa Inteligente é um projeto acadêmico desenvolvido com o objetivo de automatizar e monitorar as condições ambientais de uma pequena estufa para cultivo de hortaliças.

O sistema utiliza sensores conectados a um ESP32 para coletar informações sobre temperatura, umidade do ar, umidade do solo e luminosidade. O próprio ESP32 decide quando ligar a bomba d'água e a luminária. Os dados são enviados por Wi-Fi para uma API REST desenvolvida em Python, que armazena as leituras e alimenta um dashboard web.

O projeto também permite o controle manual da irrigação e da iluminação pelo dashboard.


## Funcionalidades

- Monitoramento da temperatura do ambiente.
- Monitoramento da umidade do ar.
- Monitoramento da umidade do solo.
- Monitoramento da luminosidade.
- Irrigação automática e manual.
- Controle automático e manual da iluminação.
- Decisão local no ESP32, que continua funcionando sem Wi-Fi e sem servidor.
- Comunicação entre ESP32 e servidor via Wi-Fi, com mensagens em JSON.
- Armazenamento das leituras em banco de dados SQLite.
- Dashboard web com valores atuais, estado dos equipamentos e histórico em gráfico.

## Tecnologias utilizadas

| Tecnologia | Aplicação |
|---|---|
| C++ / Arduino | Programação do ESP32 |
| Python | Desenvolvimento do servidor |
| Flask | API REST |
| SQLite | Banco de dados |
| HTML | Estrutura do dashboard |
| CSS | Estilização do dashboard |
| JavaScript | Interatividade e gráfico do dashboard |
| Wi-Fi / HTTP / JSON | Comunicação entre dispositivos |

## Componentes eletrônicos

- ESP32
- Sensor DHT11 (temperatura e umidade do ar)
- Sensor de luminosidade BH1750
- Sensor capacitivo de umidade do solo
- Módulo relé de 2 canais
- Mini bomba d'água
- Luminária LED

### Ligações do ESP32

| Pino | Ligado a |
|---|---|
| GPIO 4 | DHT11 (dados) |
| GPIO 32 | Sensor de solo (saída analógica) |
| GPIO 18 (SDA) | BH1750 |
| GPIO 19 (SCL) | BH1750 |
| GPIO 26 | Relé da bomba (aciona em nível baixo) |
| GPIO 27 | Relé da luminária (aciona em nível baixo) |

## Arquitetura do sistema

O sistema percorre as quatro camadas de IoT:

| Camada | Componentes |
|---|---|
| Percepção | Sensores de solo, DHT11 e BH1750, relés da bomba e da luminária |
| Rede | Wi-Fi 2,4 GHz, HTTP e payload em JSON |
| Processamento | ESP32 (decisão local) e servidor Flask com SQLite |
| Aplicação | Dashboard web |

Funcionamento:

1. Os sensores coletam os dados ambientais.
2. O ESP32 processa as leituras e controla a bomba e a luminária.
3. A cada segundo, o ESP32 envia as leituras ao servidor por Wi-Fi.
4. O servidor Flask grava as informações no SQLite e responde com os comandos atuais.
5. O dashboard mostra os dados e permite enviar comandos para a bomba e a iluminação.

## Decisão local

A regra roda dentro do ESP32, com uma faixa de histerese para evitar que os equipamentos liguem e desliguem repetidamente:

- **Bomba:** liga abaixo de 15% de umidade do solo e desliga a partir de 60%.
- **Luminária:** liga abaixo de 10% de luminosidade e desliga acima de 15%.

No dashboard, cada equipamento pode ficar em modo **automático** (decide pelos sensores) ou **manual** (ligar ou desligar).

## Organização do projeto

```text
estufa-inteligente/
│
├── estufa/
│   ├── estufa.ino
│   └── secrets.example.h
│
├── static/
│   ├── style.css
│   └── app.js
│
├── templates/
│   └── index.html
│
├── server.py
├── requirements.txt
├── estufa.db          (gerado ao executar o servidor)
├── README.md
└── .gitignore
```

## Como executar o projeto

### 1. Servidor Python

Requer Python 3. Instale as dependências:

```bash
pip install -r requirements.txt
```

Execute o servidor:

```bash
python server.py
```

O banco `estufa.db` é criado automaticamente na primeira execução.

### 2. Configuração do ESP32

1. Na Arduino IDE, instale o suporte à placa **ESP32** (pacote da Espressif).
2. No Gerenciador de Bibliotecas, instale **DHT sensor library**, **Adafruit Unified Sensor** e **BH1750**.
3. Na pasta `firmware`, copie `secrets.example.h` para `secrets.h`.
4. No `secrets.h`, informe o nome e a senha do Wi-Fi e, em `SERVIDOR_URL`, o IP do computador que executa o servidor, por exemplo `http://192.168.0.10:5000/api/leituras`.
5. Abra `estufa.ino`, selecione a placa ESP32 e envie o programa.
6. Abra o Monitor Serial em 115200. A mensagem `Servidor: codigo 200` deve aparecer a cada segundo.

Observações:

- O ESP32 só conecta em redes Wi-Fi de **2,4 GHz**.
- O ESP32 e o computador precisam estar na **mesma rede**.
- Se a gravação falhar com *Wrong boot mode*, segure o botão BOOT da placa durante o envio.

### 3. Dashboard

Com o servidor em execução, acesse no navegador:

```text
http://127.0.0.1:5000/
```

Para abrir de outro aparelho da mesma rede, use `http://IP_DO_COMPUTADOR:5000/`. Não abra o `index.html` com duplo clique, pois o dashboard depende do servidor.

## API

| Método e rota | Função |
|---|---|
| `POST /api/leituras` | Recebe uma leitura do ESP32 e responde com os comandos atuais |
| `GET /api/status` | Última leitura e comandos atuais |
| `GET /api/historico?horas=N` | Leituras das últimas N horas |
| `GET` e `POST /api/bomba` | Consulta ou define o comando da bomba (`ligar`, `desligar` ou `auto`) |
| `GET` e `POST /api/luz` | Consulta ou define o comando da luminária (`ligar`, `desligar` ou `auto`) |

Exemplo de leitura enviada pelo ESP32:

```json
{"solo_pct":100,"umidade_ar":88.0,"temperatura":25.8,
 "luminosidade_pct":1,"bomba":false,"luminaria":true}
```

Resposta do servidor:

```json
{"ok":true,"cmd_bomba":"auto","cmd_luz":"auto"}
```

## Calibração do sensor de solo

A leitura do sensor é convertida em porcentagem com dois pontos medidos no próprio sensor: **3236** no ar (0%) e **1164** na água (100%). Para recalibrar, observe o valor `Solo bruto` no Monitor Serial e altere `ADC_SECO` e `ADC_MOLHADO` no `estufa.ino`.

## Segurança

As credenciais de Wi-Fi ficam no arquivo `secrets.h`, que está no `.gitignore` e não deve ser enviado ao GitHub. O arquivo `secrets.example.h` traz apenas um modelo para configuração.

Limitações conhecidas: a API não tem autenticação e a comunicação usa HTTP sem criptografia, por isso o sistema deve ser usado apenas em rede local confiável.

## Objetivo acadêmico

O projeto tem como objetivo aplicar conceitos de Internet das Coisas (IoT), programação embarcada, desenvolvimento web, comunicação entre dispositivos e armazenamento de dados, utilizando tecnologias integradas em uma solução de automação agrícola.