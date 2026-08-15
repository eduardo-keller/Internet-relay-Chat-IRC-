# Fase 1 — Trilha TRANSPORT (+ meio compartilhado)

Plano de execução passo a passo da fase 1 pelo lado do Eduardo. O contrato
técnico está em [ARCHITECTURE.md](ARCHITECTURE.md), a divisão de trabalho em
[PLANO.md](PLANO.md) e o quadro de status em [TASKS.md](TASKS.md).

Este arquivo é o **detalhamento** da linha "Fase 1" da `PLANO.md`, não um
substituto dela. Onde os dois discordarem, a `PLANO.md` vence.

---

## Progresso

**Estamos no passo 19.** Toda a implementação **desta trilha** está pronta: os
passos 0–11 e 13–18 estão verdes, `make test` dá `173 passed, 0 failed` sem
warning, e `make && make` continua dizendo "Nothing to be done". Falta o
`valgrind` do passo 12 e o merge.

Atenção ao escopo: isto é a metade TRANSPORT mais o meio compartilhado, **17 dos
25 itens** da fase 1 no `TASKS.md`. Os 8 itens `DOMAIN` (modelo de `Channel`) são
do colega e seguem `todo`, então a **fase 1 inteira** ainda não está pronta pela
definição da `PLANO.md` §3.

O passo 12 original foi **dividido**: a verificação (bloco "Pronto quando" +
`valgrind`) fica aqui, agora; o PR virou o passo 19, depois da Parte B. Decisão
do Eduardo — terminar até o 18 antes de mexer na `main`. Verificar é sobre o
código estar certo e fica barato hoje; o PR é coordenação e pode esperar.

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
| 10 | Buffer de saída | ✅ feito — 72 asserções acumuladas |
| 11 | Teto da fila de saída (SendQ) → `false` | ✅ feito — 79 asserções acumuladas |
| **12** | **Verificar a Parte A ("Pronto quando" + `valgrind`)** | 🔵 **bloco automatizado e verde; falta `valgrind`** |
| 13 | `utils::toIrcLower` / `equalsIgnoreCase` | ✅ feito — 96 asserções acumuladas |
| 14 | `utils::split` / `toString` / `parseInt` | ✅ feito — 113 asserções acumuladas |
| 15 | `utils::isValidNickname` / `isValidChannelName` | ✅ feito — 132 asserções acumuladas |
| 16 | `parseMessage`: prefixo, comando, params | ✅ feito |
| 17 | `parseMessage`: a regra do trailing | ✅ feito — 159 asserções acumuladas |
| 18 | `Replies::numeric` / `fromClient` | ✅ feito — 173 asserções acumuladas |
| **19** | **Fechar a metade TRANSPORT + SHARED (TASKS.md + merge)** | ⬜ **próximo** |

### Arquivos já criados

- `src/Client.cpp` — OCF, identidade, `prefix()`, flags de registro, desconexão
  diferida, buffers de leitura e de saída com os respectivos tetos
- `src/Utils.cpp` — casemapping, `split`, `toString`/`parseInt`, validação de
  nickname e de nome de canal
- `tests/harness.hpp` — declara `check`/`checkEqual` e os pontos de entrada
- `tests/test_client.cpp` — `runClientTests()`
- `src/Message.cpp` — `parseMessage` e o construtor de `Message`
- `tests/test_utils.cpp` — `runUtilsTests()`
- `src/Replies.cpp` — `numeric` / `fromClient`
- `tests/test_message.cpp` — `runMessageTests()`
- `tests/test_replies.cpp` — `runRepliesTests()`
- `tests/test_main.cpp` — editado: inclui o harness e chama os dois pontos de
  entrada

O `Makefile` não foi tocado e não deve ser: `src/*.cpp` e `tests/*.cpp` são
globbed. De `include/`, **só o `Limits.hpp`** foi tocado, no passo 15, para ganhar
`MAX_NICKNAME_LEN` e `MAX_CHANNEL_LEN` — adição pura, que não quebra nada e vai
nas notas do PR. Os outros headers seguem intocados: são o contrato com o colega.

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

### ✅ Passo 10 — buffer de saída

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

**Resultado:** 72 asserções acumuladas (10 novas).

**Uma correção ao que o passo pedia:** o `consumeOutput` **não precisa** limitar
`bytes` para não quebrar. O `std::string::erase(0, n)` já remove apenas
`min(n, size() - pos)`, então `consumeOutput(999999)` num buffer curto é bem
definido e só esvazia a fila. O clamp explícito ficou assim mesmo — deixa o
limite visível no código em vez de depender de um canto do padrão que o leitor
tem que lembrar, e vira load-bearing no dia em que isso deixar de ser
`std::string`. Mas ele é redundante, e vale saber disso: na avaliação, "o que
acontece se `bytes > size()`?" tem duas respostas certas aqui.

Entrou uma asserção fora da lista: um segundo `queueOutput` **anexa atrás** do
primeiro em vez de substituir. É o caso do broadcast, em que várias linhas podem
estar esperando na fila do mesmo cliente.

E o `hasPendingOutput()` é o que arma o `POLLOUT` na fase 2. Não é otimização: um
socket com espaço no buffer de envio está quase sempre gravável, então `POLLOUT`
armado o tempo todo faz o `poll()` voltar na hora toda iteração e o laço gira a
100% de CPU sem ter o que fazer — visível no `top`.

### ✅ Passo 11 — teto da fila de saída (SendQ) → `false`

**Conceitos:** "SendQ exceeded" — o cliente parou de ler (Ctrl+Z no `nc`, ou
hostil) enquanto o canal continua conversando; **decisão a tomar: checar antes e
recusar sem anexar**, o oposto do passo 9, porque aqui não há nada a salvar (a
mensagem vai ser descartada de todo jeito quando o cliente cair) e assim o teto
de 65536 é um limite **duro**; quem age no `false` na fase 2 (`sendToClient` →
`disconnectClient`).

**Teste:** 70000 `'y'` → `false` **e a fila continua vazia**; dois enfileiramentos
de 40000 → o segundo dá `false`; exatamente no limite → `true`.

**Resultado:** 79 asserções acumuladas (7 novas). Com isto os 9 itens
`TRANSPORT` da fase 1 estão `done` no `TASKS.md` — a Parte A está implementada.

Duas coisas para saber defender.

**A comparação é sobre a SOMA**, não sobre o tamanho atual. Checar só
`_outputBuffer.size() > MAX_OUTPUT_QUEUE` parece equivalente e não é: uma fila em
65535 aceitaria uma mensagem de qualquer tamanho e pararia onde essa mensagem
parasse, deixando o teto mole de novo — ultrapassável por uma mensagem inteira.
É o tipo de erro que passa nos testes quando eles só estouram o teto de uma vez.

**A assimetria com o passo 9 é justificada, não inconsistência.** Lá o chunk que
chega pode COMPLETAR uma linha, então recusar antes de olhar jogaria fora
justamente os bytes que tornariam o buffer legal. Aqui não há nada a salvar: o
cliente não está lendo, vai ser reapado, e esses bytes nunca sairiam da máquina.
Sem ganho não há razão para aceitar transitório, então 65536 é teto **duro**. Os
dois retornam `false` com o mesmo significado para quem chama — "desconecte este
cliente" — que é onde a consistência importa.

Saiu daqui uma armadilha da fase 2 que a `ARCHITECTURE.md` §4 agora documenta: o
`disconnectClient` enfileira `ERROR :<reason>`, então num cliente com a fila
cheia esse enfileiramento falha e volta direto para o `disconnectClient` —
recursão infinita se ele não sair cedo quando o cliente já está marcado.

### ⬜ Passo 12 — verificar a Parte A

Só verificação. O PR foi adiado para o passo 19; os 9 itens `TRANSPORT` já estão
`done` no `TASKS.md`.

**Rodar o bloco "Pronto quando" da `PLANO.md` §3 Fase 1, verbatim.** Ele não é
redundante com o `tests/test_client.cpp`, e a diferença é estrutural: o bloco
reusa o **mesmo `Client c`** na remontagem e na truncagem, enquanto os testes
daqui constroem um cliente novo por assunto. Ou seja, ele prova a *composição* —
que depois de extrair `command` o buffer está limpo o bastante para o cenário
seguinte começar do zero. Os testes daqui afirmam isso em cada caso separado;
"o critério oficial passa como está escrito" é outra afirmação.

✅ **Feito, e permanente em vez de descartável:** virou `testPlanoCriterion()` em
`tests/test_client.cpp`, com o fluxo e os nomes de variável (`c`/`d`/`e`)
verbatim do documento. Passa — 85 asserções no total. Ficando na suíte, o
critério oficial não consegue divergir da implementação sem quebrar o
`make test`. Os rótulos das asserções estão em inglês (`ARCHITECTURE.md` §2) e
mapeiam um-para-um nos rótulos em português da `PLANO.md`.

**`valgrind --leak-check=full ./run_tests`.** O `Client` não tem `new` nem
ponteiro cru, só `std::string` e PODs, então o esperado é limpo em trinta
segundos. O valor não é achar algo hoje: é ter uma **linha de base limpa**, para
que um vazamento que apareça no passo 16 seja obviamente do passo 16. E é barato
agora porque o `run_tests` linka **um** arquivo de implementação — depois do 18
são quatro, e o relatório vira um bisect.

---

## Parte B — meio compartilhado

Sem dono fixo na `PLANO.md` §2 ("quem estiver travado na própria trilha pega uma
delas"). **Antes de começar: avisar o colega, ou os dois escrevem o mesmo
arquivo.** Marcar o nome nos itens `SHARED` do `TASKS.md`.

Isso ficou **mais** urgente com o PR adiado para o passo 19: marcar o nome no
`TASKS.md` de uma branch que não subiu não avisa ninguém. O aviso tem que sair
por fora — empurrar a branch, ou falar com ele.

### ✅ Passo 13 — `src/Utils.cpp`: `toIrcLower` / `equalsIgnoreCase`

**Conceitos:** casemapping da RFC 2812 §2.2 — `{}|^` são as minúsculas de
`[]\~`, herança do IRC ter nascido escandinavo; `std::tolower` puro está
**errado** aqui, e o custo do erro é concreto: `Nick[42]` e `nick{42}` são o
*mesmo* nick, então errar isso deixa dois clientes ficarem com nicks que o IRC
considera iguais e o `433` nunca dispara; `std::tolower` recebe `int` e dá UB com
`char` negativo → converter para `unsigned char` primeiro.

**Teste:** o exemplo do `README.md`: `toIrcLower("Nick[42]") == "nick{42}"`.

**Resultado:** 96 asserções acumuladas (11 novas). Duas decisões.

**O `std::tolower` não é usado — o problema foi removido, não tratado.** O
conceito acima manda converter para `unsigned char` antes; em vez disso a função
é escrita à mão em ASCII puro, o que dispensa a conversão. Dois motivos: ele não
sabe nada dos pares de colchete, então os `if` existiriam de qualquer jeito e ele
só cobriria o `A`–`Z`; e ele consulta o **locale global**, onde `tolower('I')`
nem sempre é `'i'` (o caso turco é o clássico). O casemapping do IRC é definido
só sobre ASCII, então locale nenhum tem o que responder aqui.

A aritmética virou uma faixa só: `A`–`Z` é `0x41`–`0x5A`, `[ \ ]` vêm logo
depois em `0x5B`–`0x5D`, e o caso em ASCII é um bit (`0x20`) — que é exatamente
*por que* o mapeamento escandinavo funciona. `~` → `^` é a exceção, porque desce
em vez de subir.

**A direção do par `~`/`^` seguiu o contrato**, não a lógica histórica. A
`Utils.hpp` §11 e a `ARCHITECTURE.md` §5 dizem as duas que `^` é a minúscula de
`~`; os servidores reais fazem o contrário (`0x5E` era Ü e `0x7E` era ü, então
`^`→`~` mantém o `+0x20` dos outros três pares). **As duas leituras dão a mesma
resposta ao `equalsIgnoreCase`** — `nick^` e `nick~` são o mesmo nick de qualquer
forma, só muda qual vira o representante canônico. Como não há diferença
funcional, ganhou a leitura que não obriga a editar um header, que é conversa com
o colega e não commit. Custo: um `if` a mais.

Estrutura: os testes foram para `tests/test_utils.cpp` com `runUtilsTests()`, não
dentro do `test_client.cpp` — a `harness.hpp` pede um arquivo por unidade
justamente para as duas trilhas nunca editarem o mesmo arquivo de teste.

### ✅ Passo 14 — `utils::split` / `toString` / `parseInt`

**Conceitos:** `split` **preserva campos vazios** de propósito
(`JOIN #a,,#b` não pode virar silenciosamente dois canais); `std::ostringstream`
porque C++98 não tem `std::to_string`; `parseInt` devolve `bool` + parâmetro de
saída porque tem que **recusar** não-dígito e overflow (quem usa: `MODE +l`).

**Teste:** `split("#a,,#b", ',')` → 3 campos; `toString(-42)`; `parseInt("12x")`
→ false. **Decisão tomada:** `split("", ',')` devolve **1 campo vazio** — ver
abaixo.

**Resultado:** 113 asserções acumuladas (17 novas).

**A decisão: `split("", ',')` → 1 campo vazio, não 0 campos.** Duas razões. A
invariante fica dizível numa linha — o resultado sempre tem
**(delimitadores + 1) campos** — e escolher 0 para a string vazia seria uma
exceção aberta numa função que fora isso é uniforme (`"a,"` já dá
`["a", ""]`). E o que decide de vez é o comportamento a jusante: `JOIN :` chega
no `split("")`; com 1 campo vazio ele desce para o `isValidChannelName`, falha,
e o cliente recebe um `403` de verdade. Com 0 campos o corpo do laço não roda e
um comando que o cliente realmente mandou é respondido com **silêncio**.

**Por que campos vazios são preservados**, com um motivo mais forte que o do
enunciado: o IRC casa listas paralelas por **posição**. `JOIN #a,#b ,key2` é
legal e clientes reais mandam — `#a` sem chave, `#b` com `key2`. Colapsar o campo
vazio faz a lista de chaves virar `["key2"]`, que então cai no `#a`: o usuário é
barrado num canal e entrega a chave em outro.

**No `parseInt`, a checagem de overflow roda ANTES da multiplicação.** Checar
depois é o erro comum e já é comportamento indefinido — o overflow de inteiro
com sinal aconteceu antes de haver o que inspecionar. Sinal `+`/`-` é aceito (a
função responde "isto é um inteiro?"; se um negativo faz *sentido* é pergunta do
`cmdMode`), e `INT_MIN` é recusado como overflow, porque um acumulador `int` não
guarda a magnitude dele — recusado explicitamente em vez de embrulhar em
silêncio.

### ✅ Passo 15 — `utils::isValidNickname` / `isValidChannelName`

**Conceitos:** a gramática de nickname da RFC 2812 §2.3.1 (letra ou especial
``[]\`_^{|}`` primeiro, depois letra/dígito/especial/`-`); nome de canal começa
com `#`, sem espaço, sem vírgula, sem `:`, sem `\a`; o valor de retorno é a
resposta inteira — sem string de erro, quem chama decide o numeric (`432` ou
`403`).

**Decisões tomadas e anotadas no `ARCHITECTURE.md`** (§5 "Name validation" e a
tabela da §11): o limite de nickname e o prefixo de canal — detalhes abaixo.

**Teste:** os exemplos do `README.md` — `isValidNickname("alice")` true,
`isValidNickname("4lice")` false (não pode começar com dígito).

**Resultado:** 132 asserções acumuladas (19 novas).

**Nickname: 30, não os 9 da RFC.** O número da RFC é de 1988; todo servidor real
subiu, e o irssi preenche o nick a partir do usuário do sistema, então 9
recusaria logins reais antes de a pessoa digitar qualquer coisa. Algum teto é
obrigatório de todo jeito: o nick entra no `:nick!user@host` de **toda** mensagem
que o cliente origina, e esse prefixo é cobrado do `MAX_PAYLOAD_LEN` — é limite
de recurso disfarçado de validação. Canal fica com os 50 da própria RFC.

**Só `#` como prefixo.** A RFC 2812 lista também `&`, `+` e `!`. O `+` significa
"não suporta modos", o que contradiz o `MODE` que o subject exige; o `!` precisa
de IDs de canal gerados; e o `&` significa *server-local* — como o subject
proíbe ligação servidor-servidor, todo canal aqui **já é** local, então `&` seria
um segundo prefixo com comportamento idêntico. `&foo` falha na validação e quem
chama responde `403`.

**Uma observação que fecha o passo 13:** o conjunto `special` da RFC é
`%x5B-60` e `%x7B-7D` — inclui `^` (0x5E) e **não** inclui `~` (0x7E). Um
nickname legal nunca contém `~`, então a direção do par `~`/`^` só pode importar
para nome de canal. Confirma, depois do fato, que aquela decisão era de baixo
risco.

**As constantes foram para o `Limits.hpp`** — a **primeira edição de header da
fase 1**. É adição pura e não muda nada existente, e o `MAX_CHANNEL_LEN` é
justamente o que evita o colega inventar um 50 próprio no modelo de `Channel`.
Entra nas notas do PR junto com a mudança da `ARCHITECTURE.md` §4.

### ✅ Passo 16 — `src/Message.cpp`: `parseMessage` (prefixo, comando, params)

**Conceitos:** a gramática
`[ ':' prefix SPACE ] command *( SPACE middle ) [ SPACE ':' trailing ]`;
varredura manual com índices vs `istringstream` (e por que a varredura ganha
aqui: o trailing preserva espaços); **linha malformada → `command` vazio, e quem
chama ignora a linha sem responder nada** ([ARCHITECTURE.md](ARCHITECTURE.md)
§5); o construtor `Message()` inicializa `hasTrailing = false`; clientes
normalmente **não** mandam prefixo — quem adiciona é o servidor no repasse.

**Teste:** os três exemplos da `ARCHITECTURE.md` §5, verbatim.

**Resultado:** feito **junto com o passo 17**, porque a divisão não corta limpo:
o primeiro dos três exemplos da §5 é `"PRIVMSG #chan :hello world"`, que já
exige a regra do trailing para não virar dois params. Separar significaria
escrever uma versão sabidamente errada para consertar em seguida.

**Varredura manual, não `istringstream`** — e não por velocidade. O trailing tem
de preservar os espaços internos, então no instante em que o marcador aparece o
parser precisa de "o resto cru da linha a partir daqui", que é uma operação
**posicional**. Com `>>` a posição é consumida e recuperá-la depois é chato de
fazer e pior de defender. Com índice é `substr(i + 1)`.

**O prefixo é parseado mesmo sem ninguém ler `msg.prefix`.** Os handlers sabem
quem mandou pelo `Client &sender`. Mas ele precisa no mínimo ser **pulado**:
deixe-o no lugar e `":foo PRIVMSG #chan :hi"` faz de `":foo"` o nome do comando
e perde o comando real. Já que se pula, guardar sai de graça.

**Decisão pequena, registrada em comentário:** o `parseMessage` **não** passa o
comando para maiúsculas. A tabela de despacho é chaveada em maiúsculas
(`ARCHITECTURE.md` §4), mas normalizar aqui destruiria informação que um parser
não tem por que descartar — e o `421 <command> :Unknown command` devolve o
comando ao cliente, então quem digitou `privmsgx` deve ver `privmsgx`. Quem
converte é o dispatcher, que também é "um lugar só". Isso mora inteiro na trilha
TRANSPORT, então não mexe no contrato.

### ✅ Passo 17 — `parseMessage`: a regra do trailing

**Conceitos:** o trailing começa no **primeiro `" :"`** e vai até o fim da linha,
espaços inclusos; entra como **último elemento de `params`** com o `:` removido;
`hasTrailing` existe porque **`":"` sozinho é um trailing vazio legal** e tem que
ser distinguível de "nenhum parâmetro" (ver a correção no Resultado); um `:` que
**não** vem depois de espaço não é marcador (`PRIVMSG #chan a:b` tem um param só).

**Teste:** `"PRIVMSG #chan :hello world"` → `params == ["#chan","hello world"]`;
`"PRIVMSG #chan :"` → `hasTrailing` true e último param vazio;
`"PRIVMSG #chan a:b"` → um param.

**Resultado:** 159 asserções acumuladas (27 novas, cobrindo os passos 16 e 17
juntos).

**Correção ao enunciado do teste acima:** `"PRIVMSG #chan a:b"` dá **dois**
params, `["#chan", "a:b"]`. O "um param só" do bloco de conceitos quer dizer que
`a:b` fica inteiro em **um** param em vez de o `:` virar marcador de trailing — a
linha do teste ficou ambígua. O teste implementado afirma `params.size() == 2`.

**O marcador é um `:` no INÍCIO de um parâmetro**, não um `:` em qualquer lugar.
É isso que separa `PRIVMSG #chan a:b` (dois params, sem trailing) de
`PRIVMSG #chan :a b` (dois params, o último sendo o trailing `a b`). Depois de um
marcador de verdade tudo é conteúdo — espaços e novos `:` inclusive, o que faz
`":see http://x for more"` chegar inteiro.

**Correção de numeric, e do peso do `hasTrailing`.** O bloco de conceitos dizia
que o `hasTrailing` separa `PRIVMSG #chan :` (`412`) de `PRIVMSG #chan` (`411`).
Está errado. O `411 ERR_NORECIPIENT` é `:No recipient given` e o
`PRIVMSG #chan` **tem** destinatário — o que falta é texto, que é `412`. Quem
ganha `411` é um `PRIVMSG` pelado, sem parâmetro nenhum. Conferir contra o irssi
na fase 3, como a `ARCHITECTURE.md` §10 manda para qualquer numeric. A tabela da
§6 já estava certa; era só o exemplo daqui.

E o `hasTrailing` pesa menos do que aquela frase sugeria: `PRIVMSG #chan :` dá
`params.size() == 2` e `PRIVMSG #chan` dá `1`, então **o tamanho já separa os
dois**. O valor real do campo é ser a garantia permanente de que um trailing
vazio é *empurrado* como param em vez de sumir — uma implementação que o
descartasse deixaria as duas linhas idênticas.

### ✅ Passo 18 — `src/Replies.cpp`: `numeric` / `fromClient`

**Conceitos:** as duas formas de saída ([ARCHITECTURE.md](ARCHITECTURE.md) §6);
**zero-padding em 3 dígitos** — `RPL_WELCOME` é o `int` 1 mas tem que sair como
`001`, e mandar `1` faz o irssi ignorar a linha e ficar esperando para sempre (é
o bug clássico desta função); **nenhuma das duas anexa CRLF** — o `sendToClient`
é o dono do `\r\n`, e duplicar isso manda uma linha em branco que confunde o
irssi; `target` é o nick do destinatário, ou `*` antes do registro.

**Teste:** `numeric("irc.local", 1, "*", ":Welcome")` começa com
`":irc.local 001 * "`; conferir o padding em 1, 2, 3, 4 e que 421 sai intacto;
`fromClient("a!b@c", "JOIN", "#x")` → `":a!b@c JOIN #x"`.

**Resultado:** 173 asserções acumuladas (14 novas). **Última etapa de
implementação da fase 1.**

O ponto central do passo se confirma: a RFC 2812 §2.4 define um numeric como um
número de **três dígitos**, então o `RPL_WELCOME` (o `int` 1) tem que sair como
`001`. Mandar `1` faz o irssi nem reconhecer a linha como numeric — ele nunca vê
o welcome e espera para sempre.

**Correção 1: "421 sai sem padding" não é bem isso.** O 421 não é um código que
pula o padding; ele já tem três dígitos, então a mesma operação é um no-op. Ler
como caso especial produziria um `if` sem motivo. O teste afirma que um código
de três dígitos passa **intacto**.

**Correção 2: `args` vazio deixava um espaço sobrando.** `fromClient("a!b@c",
"QUIT", "")` daria `":a!b@c QUIT "`, e o `sendToClient` gruda o CRLF logo depois
desse espaço — um parser estrito lê isso como um parâmetro vazio a mais. Um `if`
em cada função resolve. Nenhum chamador da tabela de numerics manda `args` vazio
hoje; é para a função ser total, não por causa de um bug existente.

**O padding foi construído sobre o `utils::toString`**, não sobre `<iomanip>`,
para o projeto ter um caminho só de int→string — e porque o `setw()` vale apenas
para a próxima saída enquanto o `setfill()` é sticky, distinção que não vale a
pena depender aqui.

### ⬜ Passo 19 — fechar a metade TRANSPORT + SHARED

Era a segunda metade do passo 12; foi adiado para cá de propósito.

**Sem PR.** A `PLANO.md` §5 pede PR mesmo sendo dois, porque o PR é o mecanismo
de alfabetização. Aqui não há o que alfabetizar em paralelo: o colega ainda não
começou a trilha DOMAIN, então o merge vai direto para a `main`, de comum acordo.
A regra do PR volta a valer da fase 2 em diante, quando as duas trilhas se tocam.
A legenda do `TASKS.md` foi ajustada para registrar essa exceção.

**Antes do merge:** o `valgrind` (a parte do passo 12 que ficou aberta), um
`make re` limpo, `make && make` sem relink, e o `./ircserv 6667 secret` ainda de
pé — a fase 1 acrescentou três `.cpp` que, por causa do glob, agora linkam **no
`ircserv` também**, então o binário da fase 0 precisa continuar funcionando.

**Três coisas para falar com o colega.** Não são PR, são conversa — mas duas
delas restringem o `Channel` que ele vai escrever:

1. `ARCHITECTURE.md` §4: o `disconnectClient` tem que **retornar imediatamente**
   se o cliente já está marcado. Ele enfileira `ERROR :<reason>`, então num
   cliente com a fila de saída cheia esse enfileiramento falha e volta para o
   próprio `disconnectClient` — recursão infinita. É alcançável a partir de
   qualquer handler dele, porque todo comando chama `sendToClient`.
2. `ARCHITECTURE.md` §5: **`#` é o único prefixo de canal**, e o nickname vai até
   30. Se ele aceitar `&`, vocês divergem no primeiro `JOIN`.
3. `Limits.hpp` ganhou `MAX_NICKNAME_LEN` (30) e `MAX_CHANNEL_LEN` (50). É a
   única edição de header da fase 1, puramente aditiva — e é justamente o que
   evita ele inventar um 50 próprio no modelo de `Channel`.

**Isto não fecha a fase 1.** Pela `PLANO.md` §3, a fase 1 só está pronta quando o
modelo de `Channel` também estiver construído e testado — são 8 itens `DOMAIN`
ainda `todo`. O que fecha aqui são **17 dos 25 itens**: a trilha TRANSPORT
inteira mais o meio compartilhado. A fase completa depende do colega.

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
