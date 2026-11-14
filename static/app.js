const $ = id => document.getElementById(id);

const INTERVALO_MS = 2000;

const PCT_SECO = 30;
const PCT_ENCHARCADO = 85;

let horas = 1;
let leituras = [];

const seriesAtivas = {
    solo_pct: true,
    umidade_ar: true,
    luminosidade_pct: true
};

const corDaSerie = {
    solo_pct: '#38845A',
    umidade_ar: '#5286C7',
    luminosidade_pct: '#C49A45'
};

const nomeDaSerie = {
    solo_pct: 'Solo',
    umidade_ar: 'Ar',
    luminosidade_pct: 'Luz'
};


// AUXILIARES

function formatarHora(timestamp) {
    return new Date(timestamp * 1000).toLocaleTimeString('pt-BR', {
        hour: '2-digit',
        minute: '2-digit'
    });
}

function estadoDoSolo(porcentagem) {
    if (porcentagem < PCT_SECO) {
        return ['Seco', '#c48a32'];
    }

    if (porcentagem >= PCT_ENCHARCADO) {
        return ['Muito úmido', '#5286c7'];
    }

    return ['Adequado', '#2f8a54'];
}

function atualizarSolo(valor) {
    const valorSeguro = Math.max(0, Math.min(100, valor));

    $('soloValor').textContent = Math.round(valorSeguro);

    const [estado, cor] = estadoDoSolo(valorSeguro);

    $('soloEstado').textContent = estado;
    $('soloEstado').style.color = cor;
}

function mostrarAtuador(idCard, idTexto, ligado) {
    $(idCard).classList.toggle('on', ligado);

    $(idTexto).textContent = ligado ? 'Ligada' : 'Desligada';
}

function marcarModo(rota, acao) {
    const acaoValida = ['ligar', 'desligar', 'auto'].includes(acao);

    document.querySelectorAll(
        '.botao[data-rota="' + rota + '"]'
    ).forEach(botao => {
        botao.classList.toggle(
            'ativo',
            acaoValida && botao.dataset.acao === acao
        );
    });

    const texto = rota === 'bomba'
        ? $('bombaModo')
        : $('luzModo');

    if (!acaoValida) {
        texto.textContent = 'Modo não informado';
        return;
    }

    texto.textContent = acao === 'auto'
        ? 'Modo automático'
        : 'Modo manual';
}

function atualizarConexao(online) {
    const conexao = document.querySelector('.connection');

    conexao.classList.toggle('on', online);

    $('textoStatus').textContent = online
        ? 'ESP32 conectado'
        : 'ESP32 sem comunicação';
}


// COMANDOS DOS EQUIPAMENTOS

async function enviarComando(rota, acao) {
    const botoes = document.querySelectorAll(
        '.botao[data-rota="' + rota + '"]'
    );

    botoes.forEach(botao => botao.disabled = true);

    try {
        const resposta = await fetch('/api/' + rota, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                acao: acao
            })
        });

        if (!resposta.ok) {
            throw new Error('Erro ao enviar comando');
        }

        // A confirmação visual definitiva vem da próxima leitura
        // de status da API.
        $('textoStatus').textContent = 'Comando enviado';

        await carregar();

    } catch (erro) {
        console.error('Erro no comando:', erro);

        $('textoStatus').textContent =
            'Falha ao enviar comando';

    } finally {
        botoes.forEach(botao => botao.disabled = false);
    }
}


// GRÁFICO

function desenharGrafico() {
    const canvas = $('grafico');

    if (!canvas) return;

    const ctx = canvas.getContext('2d');

    const escala = window.devicePixelRatio || 1;

    const largura = canvas.clientWidth;
    const altura = canvas.clientHeight;

    if (!largura || !altura) return;

    canvas.width = largura * escala;
    canvas.height = altura * escala;

    ctx.setTransform(escala, 0, 0, escala, 0, 0);

    const margemEsq = 38;
    const margemTopo = 12;
    const margemBase = 25;

    const alturaUtil = altura - margemTopo - margemBase;

    const estilo = getComputedStyle(document.body);

    const corBorda = estilo.getPropertyValue('--borda').trim();
    const corTexto = estilo.getPropertyValue('--texto-secundario').trim();

    ctx.clearRect(0, 0, largura, altura);

    ctx.font = '11px Segoe UI, Arial';

    // Linhas de referência

    for (let i = 0; i <= 4; i++) {
        const y = margemTopo + alturaUtil * i / 4;

        ctx.beginPath();
        ctx.moveTo(margemEsq, y);
        ctx.lineTo(largura - 5, y);

        ctx.strokeStyle = corBorda;
        ctx.lineWidth = 1;
        ctx.stroke();

        ctx.fillStyle = corTexto;

        ctx.fillText(
            String(100 - 25 * i),
            5,
            y + 4
        );
    }

    const pontosValidos = leituras.filter(ponto =>
        Number.isFinite(Number(ponto.ts))
    );

    if (pontosValidos.length < 2) {
        ctx.fillStyle = corTexto;
        ctx.textAlign = 'center';

        ctx.fillText(
            'Aguardando dados históricos...',
            largura / 2,
            altura / 2
        );

        ctx.textAlign = 'start';

        canvas.pontos = [];
        return;
    }

    const passo = Math.max(
        1,
        Math.ceil(pontosValidos.length / 300)
    );

    const pontos = pontosValidos.filter(
        (_, i) => i % passo === 0
    );

    if (pontos[pontos.length - 1] !== pontosValidos[pontosValidos.length - 1]) {
        pontos.push(pontosValidos[pontosValidos.length - 1]);
    }

    const larguraUtil = largura - margemEsq - 8;

    const posX = i =>
        margemEsq + larguraUtil * i / (pontos.length - 1);

    const posY = valor =>
        margemTopo + alturaUtil * (
            1 - Math.max(0, Math.min(100, valor)) / 100
        );

    // Séries

    Object.keys(seriesAtivas)
        .filter(serie => seriesAtivas[serie])
        .forEach(serie => {

            const valores = pontos.map(ponto =>
                Number(ponto[serie])
            );

            if (!valores.every(Number.isFinite)) return;

            const cor = corDaSerie[serie];

            ctx.beginPath();

            valores.forEach((valor, i) => {
                const x = posX(i);
                const y = posY(valor);

                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            });

            ctx.strokeStyle = cor;
            ctx.lineWidth = 2;
            ctx.lineJoin = 'round';
            ctx.lineCap = 'round';
            ctx.stroke();
        });

    // Horários

    ctx.fillStyle = corTexto;

    const primeiraHora = formatarHora(pontos[0].ts);
    const ultimaHora = formatarHora(
        pontos[pontos.length - 1].ts
    );

    ctx.fillText(
        primeiraHora,
        margemEsq,
        altura - 5
    );

    ctx.textAlign = 'right';

    ctx.fillText(
        ultimaHora,
        largura - 5,
        altura - 5
    );

    ctx.textAlign = 'start';

    canvas.pontos = pontos;
    canvas.posX = posX;
}


// TOOLTIP DO GRÁFICO

$('grafico').addEventListener('mousemove', evento => {
    const canvas = $('grafico');

    if (!canvas.pontos || canvas.pontos.length === 0) {
        return;
    }

    const caixa = canvas.getBoundingClientRect();

    const mouseX = evento.clientX - caixa.left;

    let maisPerto = 0;
    let menorDistancia = Infinity;

    canvas.pontos.forEach((_, i) => {
        const distancia = Math.abs(
            canvas.posX(i) - mouseX
        );

        if (distancia < menorDistancia) {
            menorDistancia = distancia;
            maisPerto = i;
        }
    });

    const ponto = canvas.pontos[maisPerto];

    const dica = $('dica');

    const linhas = Object.keys(seriesAtivas)
        .filter(serie => seriesAtivas[serie])
        .map(serie => {
            const valor = Number(ponto[serie]);

            if (!Number.isFinite(valor)) return '';

            return `
                <div>
                    <span style="color:${corDaSerie[serie]}">●</span>
                    ${nomeDaSerie[serie]}:
                    ${Math.round(valor)}%
                </div>
            `;
        })
        .filter(Boolean);

    dica.innerHTML = `
        <strong>${formatarHora(ponto.ts)}</strong>
        ${linhas.join('')}
    `;

    dica.style.display = 'block';

    const posicaoX = canvas.posX(maisPerto);

    dica.style.left = Math.max(
        5,
        Math.min(posicaoX + 12, caixa.width - dica.offsetWidth - 5)
    ) + 'px';

    dica.style.top = '10px';
});

$('grafico').addEventListener('mouseleave', () => {
    $('dica').style.display = 'none';
});


// ATUALIZAÇÃO DOS DADOS

async function carregar() {
    try {
        const resposta = await fetch('/api/status');

        if (!resposta.ok) {
            throw new Error('Falha ao consultar status');
        }

        const status = await resposta.json();

        if (status.erro) {
            atualizarConexao(false);
            return;
        }

        // Sensores

        atualizarSolo(Number(status.solo_pct));

        $('arValor').textContent =
            Math.round(status.umidade_ar);

        $('tempValor').textContent =
            Number(status.temperatura).toFixed(1);

        $('luzValor').textContent =
            Math.round(status.luminosidade_pct);

        // Equipamentos

        mostrarAtuador(
            'cardBomba',
            'bombaEstado',
            !!status.bomba
        );

        mostrarAtuador(
            'cardLuz',
            'luzEstado',
            !!status.luminaria
        );

        marcarModo('bomba', status.cmd_bomba);
        marcarModo('luz', status.cmd_luz);

        // Conexão ESP32

        const online =
            Date.now() / 1000 - Number(status.ts) < 30;

        atualizarConexao(online);

        $('atualizado').textContent =
            'Atualizado às ' + formatarHora(status.ts);

        // Histórico

        const respostaHistorico = await fetch(
            '/api/historico?horas=' + horas
        );

        if (!respostaHistorico.ok) {
            throw new Error('Falha ao consultar histórico');
        }

        const historico = await respostaHistorico.json();

        leituras = Array.isArray(historico)
            ? historico
            : [];

        desenharGrafico();

    } catch (erro) {
        console.error('Erro ao atualizar painel:', erro);

        atualizarConexao(false);
    }
}


// EVENTOS DOS BOTÕES

document.querySelectorAll('.botao[data-rota]').forEach(botao => {
    botao.addEventListener('click', () => {
        enviarComando(
            botao.dataset.rota,
            botao.dataset.acao
        );
    });
});


// FILTROS DAS SÉRIES

document.querySelectorAll(
    '#filtroSeries .filtro'
).forEach(botao => {

    botao.addEventListener('click', () => {

        const serie = botao.dataset.serie;

        seriesAtivas[serie] = !seriesAtivas[serie];

        botao.classList.toggle('ativo');

        desenharGrafico();
    });
});


// FILTROS DE PERÍODO

document.querySelectorAll(
    '#filtroPeriodos .filtro'
).forEach(botao => {

    botao.addEventListener('click', () => {

        document.querySelectorAll(
            '#filtroPeriodos .filtro'
        ).forEach(b => b.classList.remove('ativo'));

        botao.classList.add('ativo');

        horas = parseFloat(botao.dataset.horas);

        carregar();
    });
});


// REDIMENSIONAMENTO

window.addEventListener('resize', desenharGrafico);


// INICIALIZAÇÃO

carregar();

setInterval(carregar, INTERVALO_MS);