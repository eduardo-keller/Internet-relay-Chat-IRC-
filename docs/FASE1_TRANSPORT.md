# Fase 1 — Trilha TRANSPORT (+ meio compartilhado)

Plano de execução passo a passo da fase 1 pelo lado do Eduardo. O contrato
técnico está em [ARCHITECTURE.md](ARCHITECTURE.md), a divisão de trabalho em
[PLANO.md](PLANO.md) e o quadro de status em [TASKS.md](TASKS.md).

Este arquivo é o **detalhamento** da linha "Fase 1" da `PLANO.md`, não um
substituto dela. Onde os dois discordarem, a `PLANO.md` vence.

---

## Progresso

**Estamos no passo 10.** Os passos 0–9 estão verdes: `make test` dá
`62 passed, 0 failed`, sem warning, e `make && make` continua dizendo
"Nothing to be done".

| Passo | O quê | Status |
|---|---|---|
| 0 | Linha de base (`make re && make test` verde) | ✅ feito |
| 1 | `src/Client.cpp` nasce: OCF completa | ✅ feito — 5 asserções |
| 2 | Identidade e `prefix()` | ✅ feito — 11 asserções acumuladas |
| 3 | Flags de registro e `isRegistered()` derivado | ✅ feito — 19 asserções acumuladas |
| 4 | Desconexão diferida | ✅ feito — 23 asserções acumuladas |
| 5 | `appendToReadBuffer`, sem teto ainda | ✅ feito |
| 6 | `extractCommand`: caso feliz, `\r\n` e `\n` sozinho | ✅ feito — 39 asserções acumuladas |
| 7 | Truncar a linha em `MAX_PAYLOAD_LEN` (510) | ✅ feito — 45 asserções acumuladas |
| 8 | Sanitizar: NUL, `\r` solto, `\n` solto | ✅ feito — 56 asserções acumuladas |
| 9 | Teto do buffer de leitura → `false` | ✅ feito — 62 asserções acumuladas |
| **10** | **Buffer de saída** | ⬜ **próximo** |
| 11 | Teto da fila de saída (SendQ) → `false` | ⬜ |
| 12 | Fechar a Parte A (TASKS.md + PR) | ⬜ |
| 13 | `utils::toIrcLower` / `equalsIgnoreCase` | ⬜ |
| 14 | `utils::split` / `toString` / `parseInt` | ⬜ |
| 15 | `utils::isValidNickname` / `isValidChannelName` | ⬜ |
| 16 | `parseMessage`: prefixo, comando, params | ⬜ |
| 17 | `parseMessage`: a regra do trailing | ⬜ |
| 18 | `Replies::numeric` / `fromClient` | ⬜ |

### Arquivos já criados

- `src/Client.cpp` — OCF, identidade, `prefix()`, flags de registro
- `tests/harness.hpp` — declara `check`/`checkEqual` e os pontos de entrada
- `tests/test_client.cpp` — `runClientTests()`
- `tests/test_main.cpp` — editado: inclui o harness, chama `runClientTests()`

Nem o `Makefile` nem nenhum header de `include/` foi tocado, e não devem ser:
`src/*.cpp` e `tests/*.cpp` são globbed, e os headers são o contrato com o
colega.

---

## Contexto

### O problema que a fase 1 resolve

O `ft_irc` tem um servidor de rede no coração, mas a parte de rede é a menor
parte do código. A decisão central que a [ARCHITECTURE.md](ARCHITECTURE.md) já
fixou é:

> **A lógica recebe strings e objetos. A lógica nunca toca em file descriptors.**

Isso não é elegância gratuita — é o que torna a fase 1 possível. Se o
reagrupamento de pacotes, as flags de registro e o parser só existissem *dentro*
do laço de `poll()`, a única forma de testá-los seria abrir um socket, conectar
um `nc` e olhar bytes. Debugar assim é lento e não prova nada de forma repetível.

A fase 1 é a aposta oposta: **construir e provar tudo o que não precisa de rede,
com strings construídas à mão, antes de existir um único socket.** Quando a fase
2 chegar, o laço de `poll()` vira cola sobre peças já confiáveis — e é
exatamente por isso que ele pode ser feito em uma sessão conjunta curta, que é o
que a [PLANO.md](PLANO.md) §2 combina.

### O que a fase 1 entrega, concretamente

Estado do repo no início: `include/` com os 8 headers de contrato completos e
comentados, `src/` com **só** `main.cpp` (validação de argumentos), `tests/` com
o harness e um teste-placeholder. **Nenhuma declaração dos headers tinha
definição.** A fase 1 é escrever os `.cpp` que não dependem de rede.

Do lado TRANSPORT, isso é exatamente um arquivo: **`src/Client.cpp`**. O
`Client` é o objeto que guarda tudo que um cliente conectado tem *além* do fd:
identidade, progresso do registro, o buffer de entrada meio-cozido e a fila de
saída ainda não escrita. As duas funções que justificam a fase inteira são:

- **`appendToReadBuffer`** — TCP é um fluxo de bytes, não de mensagens. Um
  `recv()` pode devolver meio comando, três comandos, ou dois e meio. Quem
  remonta isso é essa função, e o subject exige explicitamente que
  `com` + `man` + `d` cheguem como o comando `command`.
- **`extractCommand`** — o inverso: tira uma linha completa do buffer quando (e
  só quando) ela está inteira.

Do lado compartilhado, feito **depois** do `Client`: `Utils.cpp`, `Message.cpp`
e `Replies.cpp` — funções puras que as duas trilhas consomem.

### Como isso se encaixa no projeto todo

```
FASE 1 (aqui)                    FASE 2                        FASE 3
─────────────────────            ──────────────────            ──────────────
Client                    ┐      Server::handleReadable        cmdJoin
 ├ appendToReadBuffer     ├──►    recv() → append              cmdPrivmsg
 ├ extractCommand         │       while (extractCommand)        cmdKick ...
 ├ queueOutput            │         → parseMessage              (trilha DOMAIN)
 ├ flags de registro      │         → CommandTable[cmd]
 └ isDisconnecting        ┘      Server::sendToClient
                                   → queueOutput
parseMessage              ───►   poll() + POLLOUT
utils::*                         reapDisconnected
Replies::numeric
```

Cada peça da fase 1 tem **um** consumidor na fase 2, e cada passo abaixo nomeia
qual. Nada aqui é feito "por completude": se você não conseguir dizer quem chama
a função na fase 2, o passo está errado.

Três consequências que costumam surpreender e que já estão decididas nos docs:

1. **Os limites (`Limits.hpp`) entram agora, não na fase 4.** Um buffer sem teto
   é exaustão de memória disparada por um cliente que parece bem-comportado, e o
   subject exige que o servidor não quebre "even when it runs out of memory".
   Além disso, adicionar um teto depois **muda o contrato da unidade e invalida
   os testes da fase 1** ([ARCHITECTURE.md](ARCHITECTURE.md) §11).
2. **`appendToReadBuffer` e `queueOutput` retornam `bool`**, e o `false` é a
   forma de a lógica dizer "desconecte este cliente" **sem tocar num fd**. É o
   seam funcionando na prática.
3. **`isDisconnecting()` existe na fase 1** porque a desconexão é *diferida*.
   Ela não é polimento: é o que impede o use-after-free quando um pacote traz
   `QUIT :bye\r\nPRIVMSG #chan :hi\r\n`.

### O que a fase 1 NÃO faz

Nenhum `socket`, `bind`, `listen`, `accept`, `poll`, `recv`, `send`, `fcntl`,
`signal`. Nenhum `#include <sys/socket.h>` fora do que já está em `Server.hpp`.
Nenhum handler de comando (`cmdPass`/`cmdNick`/`cmdUser` são fase 2). Se um
passo abaixo te fizer querer abrir um socket para testar, o passo está errado.

---

## Como cada passo funciona

O ciclo, repetido ~18 vezes:

1. A função é escrita **completa e funcionando** em `src/*.cpp`, no estilo do
   repo (tabs, `return (x);`, comentários em inglês).
2. Explicação: o que cada linha faz, por que essa decisão e não a alternativa, e
   **quem chama isso na fase 2**.
3. Os testes do passo entram em `tests/test_client.cpp`.
4. Ler, questionar, rodar `make test`. Se algo não fecha, corrigir antes de
   seguir.

Regras que valem em todo passo, tiradas do contrato:

- C++98 estrito. Sem `auto`, `nullptr`, `std::to_string`, range-for, chaves de
  inicialização. Compila com `-Wall -Wextra -Werror -std=c++98` **sem warning**.
- Identificadores e comentários em **inglês**.
- Nada de número mágico: os limites vêm de `include/Limits.hpp`.

---

## Parte A — `src/Client.cpp` (trilha TRANSPORT)

Cobre os 9 itens `TRANSPORT` da Fase 1 no [TASKS.md](TASKS.md).

### ✅ Passo 0 — linha de base (sem código novo)

`make re && make test` → `1 passed, 0 failed`. `make && make` → "Nothing to be
done" (prova que os `.d` do `-MMD -MP` estão funcionando e não há relink).

### ✅ Passo 1 — o arquivo nasce: OCF completa

**Arquivos:** `src/Client.cpp` (novo), `tests/harness.hpp` (novo),
`tests/test_client.cpp` (novo), `tests/test_main.cpp` (chamar
`runClientTests()`, apagar o placeholder `"harness works"`).

**Funções:** `Client()`, `Client(int, const std::string&)`, construtor de cópia,
`operator=`, `~Client()`, `getHostname()` e o privado `getFd()`.

**Conceitos:** OCF (Orthodox Canonical Form) em C++98 e por que a 42 cobra as
quatro; lista de inicialização vs atribuição no corpo — e por que a **ordem**
importa (`-Wreorder` está em `-Wall`, e com `-Werror` vira falha de build);
`_fd = -1` como sentinela; **por que um construtor de cópia é uma armadilha numa
classe que guarda um fd** (dois objetos, um fd; o kernel recicla números de fd,
então um `close` duplo fecha a conexão de *outro* cliente) e como o contrato
desvia disso — o `Server` guarda `Client*` num `std::map<int, Client*>`, e o
destrutor do `Client` **não** fecha o fd (quem fecha é `reapDisconnected`);
guarda de auto-atribuição; `getFd()` privado + `friend class Server` como forma
de transformar convenção em **erro de compilação**.

**Por que `getHostname()` entrou já aqui:** a OCF não tem efeito observável.
Sem um getter, o teste do passo só poderia checar "compilou". O hostname é o
membro fixado na construção que nunca muda — exatamente o estado cuja
sobrevivência à cópia queremos provar.

**Resultado:** 5 asserções verdes. A linha de link confirmou o essencial —
`obj/Client.o obj/tests/*.o -o run_tests`, com o `main.o` de fora, que é o que o
`LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o, $(OBJS))` existe para fazer.

### ✅ Passo 2 — identidade e `prefix()`

**Funções:** `getNickname`/`getUsername`/`getRealname`, os 3 setters, `prefix()`.

**Conceitos:** por que os getters devolvem `const std::string&` (zero cópia, e o
membro sobrevive à chamada) mas `prefix()` devolve **por valor** — a
concatenação é local, e devolver referência para ela seria uma referência
pendurada que costuma "funcionar" nos testes e quebrar em produção; o `const` no
fim da assinatura, sem o qual o método não pode ser chamado através do
`const Client &origin` que o `broadcastToPeers` recebe; por que não existe
`setHostname` (vem do `accept()` e nunca muda); por que `prefix()` **não é
cacheado** — `NICK` o invalida, e um cache velho relaia o nick antigo para o
canal inteiro, bug que só aparece depois de alguém rodar `/nick`; o formato
`nick!user@host` e o fato de os dois-pontos iniciais serem do
`Replies::fromClient`, não daqui.

**Resultado:** 11 asserções acumuladas.

### ✅ Passo 3 — flags de registro e `isRegistered()` derivado

**Funções:** `hasPass`/`hasNick`/`hasUser`, os 3 setters, `isRegistered()`,
`welcomeSent()`/`setWelcomeSent()`.

**Conceitos:** estado **derivado** vs **guardado** — `isRegistered()` é
`_hasPass && _hasNick && _hasUser` calculado na hora, e não existe
`setRegistered()` de propósito, porque duas fontes de verdade para o mesmo fato
sempre acabam discordando (três handlers teriam que lembrar de sincronizar, e
esquecer num deles trava o cliente em `451` para sempre); **nível vs borda** —
`welcomeSent` *é* guardado porque a rajada `001`–`004` tem que sair exatamente
uma vez na *transição*, e um valor derivado só conhece o nível atual, nunca a
transição; as três flags são bits independentes e não um contador, porque a
ordem real não é garantida (o irssi manda `PASS`/`NICK`/`USER` num pacote só) e
porque `NICK` volta a acontecer depois do registro; quem lê `isRegistered()` na
fase 2 é **o dispatcher**, não cada handler — é ele que responde `451`.

**Resultado:** 19 asserções acumuladas (8 novas). As três que carregam o passo
são a que limpa uma flag já registrada (prova que `isRegistered()` é derivado, e
não um `bool` guardado que ficaria para trás), a que ativa as flags fora de
ordem (`USER`, `NICK`, `PASS` — é assim que o irssi manda, num pacote só) e a
que separa `welcomeSent` de `isRegistered()`.

### ✅ Passo 4 — desconexão diferida

**Funções:** `isDisconnecting()`, `markDisconnecting()`, `getQuitReason()`.

**Conceitos:** o cenário concreto de use-after-free —
`QUIT :bye\r\nPRIVMSG #chan :hi\r\n` cabe num pacote TCP só; se `cmdQuit`
deletasse o `Client`, o laço de despacho processaria o `PRIVMSG` seguinte por uma
referência pendurada; por isso `Server::disconnectClient` **marca** e
`reapDisconnected` deleta no fim da iteração; quem lê a flag na fase 2 (o laço
de despacho para de extrair linhas; o reap decide quem morre); **decisão
tomada: primeira razão vence** (uma segunda marcação não sobrescreve), para o
`ERROR :<reason>` refletir a causa real e não a última.

**Teste:** cliente novo não está desconectando; após marcar, flag + razão; marcar
duas vezes não troca a razão.

**Resultado:** 23 asserções acumuladas (4 novas). Uma correção ao cenário acima,
notada ao escrever o passo: o pacote com `QUIT` + `PRIVMSG` **não** é o que torna
o bug alcançável. O laço de despacho é `while (client.extractCommand(line))`, e a
condição é avaliada de novo assim que o handler retorna — um `QUIT` sozinho já
basta para chamar `extractCommand` em memória liberada. O pacote colado só
aumenta o estrago (aí o `PRIVMSG` inteiro roda por uma referência pendurada).

### ✅ Passo 5 — `appendToReadBuffer`, sem teto ainda

**Conceitos:** TCP é um **fluxo de bytes sem fronteiras de mensagem** — o kernel
não tem noção de "comando IRC", e `recv()` devolve uma contagem de bytes, não uma
mensagem; por isso o buffer precisa ser **por cliente** (e o
`std::map<int, Client*>` do `Server` já dá isso de graça); por que
`std::string` e não `char*` (guarda NUL sem problema, cresce sozinho); o `bool`
de retorno é uma promessa que só se cumpre no passo 9.

**Teste (provisório):** retorna `true` e não quebra. Este é o único passo cujo
efeito não é observável sozinho — a prova real chega no passo 6, que é o par
dele. Vale como passo separado porque o conceito (fluxo de bytes) é a ideia
central da trilha.

**Resultado:** feito junto com o passo 6. O valor do passo não está nas duas
linhas de código e sim no **contrato do chamador**, que ficou registrado em
comentário: na fase 2 o `handleReadable` tem que construir a string com a
CONTAGEM que o `recv` devolveu (`std::string(buf, n)`), nunca
`std::string(buf)` — `buf` não é terminado em NUL, então a forma de um argumento
lê além dos dados e ainda trunca em qualquer NUL que o cliente mandou. E checar
`n` antes de converter: `n == 0` é o peer fechando a conexão, e
`std::string(buf, -1)` pede uma string de ~18 quintilhões de bytes.

### ✅ Passo 6 — `extractCommand`: o caso feliz, `\r\n` e `\n` sozinho

**Conceitos:** `find('\n')` e `std::string::npos`; `substr` + `erase`; **por que
procuramos `\n` e depois removemos um `\r` final, em vez de procurar `"\r\n"`
direto** — o `nc` sem `-C` manda `\n` puro, e o teste do subject usa `nc`, então
um parser que exige CRLF simplesmente não responde; o idioma
"parâmetro de saída + retorno `bool`" (C++98 não tem `std::optional`); quem chama
na fase 2 (`handleReadable` drena num `while (client.extractCommand(line))` até
dar `false`, **e para antes disso se `isDisconnecting()` virar true**).

**Teste:** o snippet canônico da `PLANO.md` (`com` → `man` → `d\r\n` → o comando
`command`); duas linhas num único append viram dois comandos; `\n` sozinho
funciona igual a `\r\n`; buffer vazio → `false`; `"\r\n"` sozinho extrai uma
linha vazia (e o dispatcher tem que ignorar linha vazia sem responder nada).

**Resultado:** 39 asserções acumuladas (16 novas, contando o passo 5). Duas
armadilhas ficaram registradas em comentário. O `+ 1` no `erase` é o próprio
terminador, e esquecê-lo devolve uma linha vazia extra antes de **cada** linha
real — como o dispatcher ignora linha vazia, o servidor continua *parecendo*
funcionar, que é o pior tipo de bug. E procurar `"\r\n"` em vez de `'\n'` não só
deixa de responder ao `nc` (que sem `-C` manda LF puro) como faz os bytes não
terminados se acumularem até o teto do passo 9 desconectar um cliente
perfeitamente bem-comportado.

### ✅ Passo 7 — truncar a linha em `MAX_PAYLOAD_LEN` (510)

**Conceitos:** RFC 2812 §2.3 — 512 bytes **incluindo** o CRLF, logo 510 de
payload; a **política é truncar e processar**, não desconectar, porque é o que
servidores reais fazem e impede que uma linha gorda vire uma queda de conexão; e
o par disso no caminho de saída (`Server::sendToClient` na fase 2), porque checar
só a entrada **não** pega o caso em que um `PRIVMSG` de 504 bytes vira 520 depois
de ganhar o prefixo `:nick!user@host `.

**Teste:** 600 `'a'` + CRLF → `out.size() == irc::MAX_PAYLOAD_LEN`; exatamente
510 passa intacto; 511 → 510.

**Resultado:** 45 asserções acumuladas (6 novas). Entrou uma que não estava na
lista acima e é justamente a que pega implementação errada: depois de truncar uma
linha de 600 bytes, o **buffer tem que ficar vazio**. Uma versão que truncasse
deixando a sobra no buffer devolveria um segundo comando montado com os 90 bytes
descartados. Aqui isso sai de graça porque o `erase(0, end + 1)` já tirou a linha
inteira antes de o `resize` rodar.

A ordem dentro do `extractCommand` ficou fixada: framing (`substr`/`erase`) →
terminador (`\r`) → teto de 510 → sanitização (passo 8). O `\r` sai **antes** do
teto porque a RFC conta o CRLF dentro dos 512, então os 510 valem para o que
sobra depois do terminador.

E o lembrete de que este passo é só **metade do par**: um `PRIVMSG` de 504 bytes
é legal na entrada e só estoura os 512 depois de ganhar o prefixo
`:nick!user@host ` na saída. Quem corta isso é o `sendToClient` na fase 2 — item
já listado no `TASKS.md`, e o único ponto por onde todo byte de saída passa.

### ✅ Passo 8 — sanitizar: NUL, `\r` solto, `\n` solto

**Conceitos:** `std::string` guarda NUL sem reclamar, mas **qualquer caminho que
chame `.c_str()` trunca silenciosamente ali** — um NUL é uma mina no parser;
onde limpar (na extração, [ARCHITECTURE.md](ARCHITECTURE.md) §11); **decisão a
tomar: truncar primeiro (o limite é de bytes na rede), limpar depois** —
consequência aceita: a linha entregue pode ter menos de 510 bytes; quem manda
esses bytes na prática (nunca o irssi; sempre o `nc` e o avaliador despejando
binário).

**Teste:** `"PING\0 :x"` sai sem o NUL; `\r` no meio da linha desaparece; lixo
binário não quebra nada.

**Resultado:** 56 asserções acumuladas (11 novas). Três coisas ficaram
decididas e registradas em comentário.

**Só esses três bytes, não todos os de controle.** "Limpar tudo abaixo de 0x20"
parece o lado seguro e é errado: `0x01` delimita CTCP, que é como o irssi manda
`/me` (`PRIVMSG #chan :\x01ACTION waves\x01`), e `0x02`/`0x03`/`0x0F`/`0x1F` são
negrito, cor, reset e sublinhado. Não implementamos CTCP — só repassamos os
bytes — então `/me` funciona de graça a menos que a gente estrague. Limpar amplo
faria o `/me` aparecer como o texto literal `ACTION waves` entre dois irssi de
verdade na fase 3. Isto bate com o que a `ARCHITECTURE.md` §11 já dizia, então
não é mudança de contrato.

**O `\n` do predicado é defensivo, não um caso vivo.** A linha foi cortada no
primeiro `\n`, logo nenhum pode estar dentro dela: injeção de CRLF já é
impossível pelo *framing*, não pela limpeza. Fica só como proteção caso o
framing mude.

**Limpar depois de truncar** (a decisão que o passo 7 antecipou): limpar antes
deixaria o cliente usar NUL de enchimento para passar payload além dos 510 — 600
bytes com 100 NUL entregariam 500 bytes reais pelo mesmo custo na rede. Ancorar
o teto nos bytes **da rede** é o que mantém o teto real. Preço aceito e testado:
uma linha de 510 com 3 NUL é entregue com 507.

### ✅ Passo 9 — teto do buffer de leitura → `false`

**Conceitos:** exaustão de memória provocada por um cliente que parece educado;
**o qualificador é o ponto todo** — estourar 4096 **sem nenhuma linha completa
dentro** é uma enchente não terminada, mas 4096 bytes *com* linhas completas é
pipelining legítimo e não pode ser punido; **decisão a tomar: anexar primeiro,
checar depois** (o `recv` traz no máximo `RECV_CHUNK`, então o pior caso
transitório é limitado e explicável, e recusar antes poderia jogar fora
justamente o pedaço que completava a linha); quem age no `false` na fase 2
(`handleReadable` → `ERROR :Request too long` + `disconnectClient`).

**Teste:** 5000 `'x'` sem CRLF → `false`; **5000 bytes com um CRLF dentro →
`true`** (é este teste que prova que você acertou o qualificador, e é o que
quase toda implementação erra).

**Resultado:** 62 asserções acumuladas (6 novas). O qualificador virou uma
condição composta só:

```cpp
if (_readBuffer.size() > irc::MAX_READ_BUFFER
	&& _readBuffer.find('\n') == std::string::npos)
	return (false);
```

O `find('\n')` é de propósito o **mesmo critério** com que o `extractCommand`
corta linha. Qualquer teste diferente aqui acabaria recusando um buffer que o
`extractCommand` teria drenado numa boa.

Duas asserções entraram fora da lista acima: o teto tem que ser **ultrapassado**,
não apenas alcançado (4096 exatos sem terminador ainda passam; 4097 não), e o
teto vale para o buffer **acumulado**, não para um chunk — um cliente pingando
1000 bytes sem terminador cai igual a quem despeja tudo de uma vez.

E o "anexar primeiro, checar depois": recusar antes poderia jogar fora justamente
o pedaço que fechava a linha (buffer em 4090 recebendo os últimos 10 bytes mais o
CRLF é legítimo). O custo é um transitório **limitado** — o `recv` pede no máximo
`RECV_CHUNK`, então o pico antes da resposta é
`MAX_READ_BUFFER + RECV_CHUNK` = 8192, explicável em vez de ilimitado.

### ⬜ Passo 10 — buffer de saída

**Funções:** `queueOutput` (sem teto ainda), `getOutputBuffer`, `consumeOutput`,
`hasPendingOutput`.

**Conceitos:** `send()` não bloqueante pode aceitar **parte** de uma escrita — o
retorno é uma contagem de bytes, não um sucesso/falha — então os bytes que
sobraram têm que ficar parados em algum lugar, e esse lugar é por cliente; o laço
arma `POLLOUT` **só** enquanto `hasPendingOutput()` for true, senão o `poll()`
volta na hora toda iteração e o laço gira a 100% de CPU; por que
`getOutputBuffer()` devolve `const&` (a fase 2 passa `.c_str()` e `.size()`
direto para o `send()`); `consumeOutput` tem que **limitar `bytes` a `size()`**
— nunca `erase` além do fim.

**Teste:** enfileirar → `hasPendingOutput()` true; `consumeOutput(3)` deixa o
resto; consumir tudo → vazio e `hasPendingOutput()` false; `consumeOutput(999999)`
não quebra.

### ⬜ Passo 11 — teto da fila de saída (SendQ) → `false`

**Conceitos:** "SendQ exceeded" — o cliente parou de ler (Ctrl+Z no `nc`, ou
hostil) enquanto o canal continua conversando; **decisão a tomar: checar antes e
recusar sem anexar**, o oposto do passo 9, porque aqui não há nada a salvar (a
mensagem vai ser descartada de todo jeito quando o cliente cair) e assim o teto
de 65536 é um limite **duro**; quem age no `false` na fase 2 (`sendToClient` →
`disconnectClient`).

**Teste:** 70000 `'y'` → `false` **e a fila continua vazia**; dois enfileiramentos
de 40000 → o segundo dá `false`; exatamente no limite → `true`.

### ⬜ Passo 12 — fechar a Parte A

Rodar o bloco "Pronto quando" inteiro da `PLANO.md` §3 Fase 1. Marcar os 9 itens
`TRANSPORT` como `done` no `TASKS.md` **no mesmo commit**. Branch
`feat/transport-buffer`, PR (a `PLANO.md` §5 pede PR mesmo sendo dois — é o
mecanismo de alfabetização).

---

## Parte B — meio compartilhado

Sem dono fixo na `PLANO.md` §2 ("quem estiver travado na própria trilha pega uma
delas"). **Antes de começar: avisar o colega, ou os dois escrevem o mesmo
arquivo.** Marcar o nome nos itens `SHARED` do `TASKS.md`.

### ⬜ Passo 13 — `src/Utils.cpp`: `toIrcLower` / `equalsIgnoreCase`

**Conceitos:** casemapping da RFC 2812 §2.2 — `{}|^` são as minúsculas de
`[]\~`, herança do IRC ter nascido escandinavo; `std::tolower` puro está
**errado** aqui, e o custo do erro é concreto: `Nick[42]` e `nick{42}` são o
*mesmo* nick, então errar isso deixa dois clientes ficarem com nicks que o IRC
considera iguais e o `433` nunca dispara; `std::tolower` recebe `int` e dá UB com
`char` negativo → converter para `unsigned char` primeiro.

**Teste:** o exemplo do `README.md`: `toIrcLower("Nick[42]") == "nick{42}"`.

### ⬜ Passo 14 — `utils::split` / `toString` / `parseInt`

**Conceitos:** `split` **preserva campos vazios** de propósito
(`JOIN #a,,#b` não pode virar silenciosamente dois canais); `std::ostringstream`
porque C++98 não tem `std::to_string`; `parseInt` devolve `bool` + parâmetro de
saída porque tem que **recusar** não-dígito e overflow (quem usa: `MODE +l`).

**Teste:** `split("#a,,#b", ',')` → 3 campos; `toString(-42)`; `parseInt("12x")`
→ false. **Decisão a tomar:** o que `split("", ',')` devolve (0 campos ou 1
vazio) — escolher e anotar.

### ⬜ Passo 15 — `utils::isValidNickname` / `isValidChannelName`

**Conceitos:** a gramática de nickname da RFC 2812 §2.3.1 (letra ou especial
``[]\`_^{|}`` primeiro, depois letra/dígito/especial/`-`); nome de canal começa
com `#`, sem espaço, sem vírgula, sem `:`, sem `\a`; o valor de retorno é a
resposta inteira — sem string de erro, quem chama decide o numeric (`432` ou
`403`).

**Decisões a tomar e anotar no `ARCHITECTURE.md`:** o limite de 9 caracteres da
RFC (servidores reais usam 30, o irssi aceita mais) e se aceitamos `&` além de
`#` como prefixo de canal.

**Teste:** os exemplos do `README.md` — `isValidNickname("alice")` true,
`isValidNickname("4lice")` false (não pode começar com dígito).

### ⬜ Passo 16 — `src/Message.cpp`: `parseMessage` (prefixo, comando, params)

**Conceitos:** a gramática
`[ ':' prefix SPACE ] command *( SPACE middle ) [ SPACE ':' trailing ]`;
varredura manual com índices vs `istringstream` (e por que a varredura ganha
aqui: o trailing preserva espaços); **linha malformada → `command` vazio, e quem
chama ignora a linha sem responder nada** ([ARCHITECTURE.md](ARCHITECTURE.md)
§5); o construtor `Message()` inicializa `hasTrailing = false`; clientes
normalmente **não** mandam prefixo — quem adiciona é o servidor no repasse.

**Teste:** os três exemplos da `ARCHITECTURE.md` §5, verbatim.

### ⬜ Passo 17 — `parseMessage`: a regra do trailing

**Conceitos:** o trailing começa no **primeiro `" :"`** e vai até o fim da linha,
espaços inclusos; entra como **último elemento de `params`** com o `:` removido;
`hasTrailing` existe porque **`":"` sozinho é um trailing vazio legal** e tem que
ser distinguível de "nenhum parâmetro" — é o que separa `PRIVMSG #chan :` (→
`412 ERR_NOTEXTTOSEND`) de `PRIVMSG #chan` (→ `411 ERR_NORECIPIENT`); um `:` que
**não** vem depois de espaço não é marcador (`PRIVMSG #chan a:b` tem um param só).

**Teste:** `"PRIVMSG #chan :hello world"` → `params == ["#chan","hello world"]`;
`"PRIVMSG #chan :"` → `hasTrailing` true e último param vazio;
`"PRIVMSG #chan a:b"` → um param.

### ⬜ Passo 18 — `src/Replies.cpp`: `numeric` / `fromClient`

**Conceitos:** as duas formas de saída ([ARCHITECTURE.md](ARCHITECTURE.md) §6);
**zero-padding em 3 dígitos** — `RPL_WELCOME` é o `int` 1 mas tem que sair como
`001`, e mandar `1` faz o irssi ignorar a linha e ficar esperando para sempre (é
o bug clássico desta função); **nenhuma das duas anexa CRLF** — o `sendToClient`
é o dono do `\r\n`, e duplicar isso manda uma linha em branco que confunde o
irssi; `target` é o nick do destinatário, ou `*` antes do registro.

**Teste:** `numeric("irc.local", 1, "*", ":Welcome")` começa com
`":irc.local 001 * "`; conferir o padding em 1, 2, 3, 4 e que 421 sai sem
padding; `fromClient("a!b@c", "JOIN", "#x")` → `":a!b@c JOIN #x"`.

---

## Verificação

**Por passo:** `make test` verde, zero warning. Quando estiver iterando, separe
para não rebuildar a cada execução:

```sh
make run_tests && ./run_tests; echo $?
```

**No fim da Parte A** — o bloco "Pronto quando" da `PLANO.md` §3, que é o
critério oficial da fase:

```cpp
Client c(3, "localhost");
std::string out;
c.appendToReadBuffer("com");
check(!c.extractCommand(out), "fragmento incompleto nao vira comando");
c.appendToReadBuffer("man");
check(!c.extractCommand(out), "ainda incompleto");
c.appendToReadBuffer("d\r\n");
check(c.extractCommand(out) && out == "command", "remontou o comando");

c.appendToReadBuffer(std::string(600, 'a') + "\r\n");
check(c.extractCommand(out) && out.size() == irc::MAX_PAYLOAD_LEN,
      "linha longa truncada em 510");

Client d(4, "localhost");
check(!d.appendToReadBuffer(std::string(5000, 'x')),
      "buffer estoura e retorna false");

Client e(5, "localhost");
check(!e.queueOutput(std::string(70000, 'y')), "output queue estoura");
```

**No fim da fase inteira:**

```sh
make re && make test          # tudo verde, zero warning
make && make                  # "Nothing to be done" — sem relink
./ircserv 6667 secret         # ainda o placeholder da fase 0; a fase 1 nao mexe nele
valgrind --leak-check=full ./run_tests
```

O `valgrind` no `run_tests` já vale a pena agora: se o `Client` vazar aqui, com
zero socket envolvido, vai vazar multiplicado por N clientes na fase 2.

**O que a fase 1 deliberadamente não prova:** conformidade de protocolo. Os
testes de unidade mostram que somos internamente consistentes, não que estamos
certos — só a RFC e o irssi mostram isso, e isso é a fase 3
([ARCHITECTURE.md](ARCHITECTURE.md) §9).

---

## Arquivos

| Arquivo | Ação | Passos |
|---|---|---|
| `src/Client.cpp` | novo | 1–11 |
| `tests/harness.hpp` | novo — declara `check`/`checkEqual` | 1 |
| `tests/test_client.cpp` | novo — `runClientTests()` | 1–11 |
| `tests/test_main.cpp` | 2 linhas: chamar `runClientTests()`, apagar o placeholder | 1 |
| `src/Utils.cpp` | novo | 13–15 |
| `src/Message.cpp` | novo | 16–17 |
| `src/Replies.cpp` | novo | 18 |
| `docs/TASKS.md` | marcar itens como `done` | 12, 18 |
| `Makefile` | **nenhuma** — `src/*.cpp` e `tests/*.cpp` são globbed | — |
| `include/*.hpp` | **nenhuma** — os headers são o contrato; mudar um é conversa com o colega, não commit | — |
