# PLANO — ft_irc

Documento de coordenação e ensino da dupla. O contrato técnico está em
[ARCHITECTURE.md](ARCHITECTURE.md) (em inglês, junto com o código). A lista de
tarefas do dia a dia está em [TASKS.md](TASKS.md).

**Orçamento:** 2 pessoas, 20 h cada (40 h no total), espalhadas por 15 dias.

Isto **não** é um calendário dia a dia. Qualquer cronograma fixo estaria errado
no dia 3. O que vale são as fases e os seus critérios de "pronto quando…", que
são observáveis: ou o comando roda e mostra o resultado esperado, ou a fase não
acabou.

---

## 1. Princípio que organiza tudo

**A lógica recebe strings e objetos. A lógica nunca toca em file descriptors.**

Consequência prática: o buffer, o parser, as validações, o modelo de `Channel`
e a máquina de estados de registro podem ser escritos e testados **antes de
existir qualquer socket**. Só o laço de `poll()`, o `accept`, o `recv`, o
`send` e a desconexão dependem mesmo de rede.

É isso que permite as duas pessoas trabalharem em paralelo sem se bloquearem.

---

## 2. Divisão do trabalho: duas trilhas verticais

Não dividimos por fase ("você faz a fase 1, eu faço a fase 2"). Cada pessoa
leva uma trilha **de ponta a ponta**, as duas correm em paralelo e se encontram
na integração.

**Modelo: PROFUNDO na sua trilha, ALFABETIZADO na do colega.**
Não temos tempo para as duas pessoas implementarem tudo — isso é trabalho em
dobro. A alfabetização vem de duas coisas: revisar o PR do colega e, em cada
checkpoint, o colega **explicar em voz alta o que construiu** (teach-back de
uns 10 minutos). Isso também é exatamente o que a avaliação da 42 cobra:
qualquer um dos dois precisa saber explicar qualquer linha.

### Trilha TRANSPORT — Eduardo

Buffer → laço de `poll()` + sockets → registro (`PASS`/`NICK`/`USER`) → ligar
as unidades testadas num servidor vivo.

É a metade conceitualmente nova (I/O não bloqueante, multiplexação, remontagem
de pacotes). É onde a avaliação mais aperta, e é o maior ganho de aprendizado.

### Trilha DOMAIN — colega

Modelo de `Channel` → handlers que rodam depois do registro (`JOIN`, `PART`,
`PRIVMSG`, `KICK`, `INVITE`, `TOPIC`, `MODE`) → a lógica deles.

É mais C++ que já dominamos (STL, containers, const correctness, OCF) aplicado
a um domínio de chat: mais abrangência, menos novidade.

### Meio compartilhado

Parser e utilitários de validação (`Message.hpp`, `Utils.hpp`) são funções
puras, sem dono fixo. Quem estiver travado na própria trilha pega uma delas.

### A única exceção à divisão

**O laço de `poll()` é construído pelas duas pessoas juntas, numa sessão
focada.** É a peça mais nova, mais arriscada e mais cobrada na avaliação.
Fazer junto tira o risco cedo e garante que os dois entendem o coração do
servidor. Depois disso divergimos de novo: Eduardo estende o laço, o colega
pluga os handlers nele.

---

## 3. Fases e checkpoints

### Fase 0 — Fundação (juntos)

Combinar e commitar os headers de contrato, o Makefile, os `.md` e um repo que
compila com um `main` vazio.

**Pronto quando:**

```sh
make && ./ircserv 6667 secret
# ircserv: would listen on port 6667 (not implemented yet)

./ircserv                 # erro de uso, sai com 1
./ircserv 80 secret       # erro de porta, sai com 1
make test                 # 1 passed, 0 failed
make && make              # "Nothing to be done" — sem relink desnecessário
```

Os dois leram `ARCHITECTURE.md` inteiro e concordam com o seam do `Server`.

---

### Fase 1 — Lógica pura, sem sockets (em paralelo, os dois)

Cada unidade é construída e testada com strings construídas à mão. Nenhum
socket em lugar nenhum.

- **Transport:** buffer de leitura (`appendToReadBuffer` / `extractCommand`),
  estado de registro no `Client`.
- **Domain:** modelo de `Channel` completo — membros, operadores, lista de
  convites, modos `i`/`t`/`k`/`l`.
- **Compartilhado:** `parseMessage`, `utils::*`.

**Pronto quando:** `make test` passa com testes reais para cada unidade, e o
teste de pacote parcial passa **no nível de unidade**:

```cpp
Client c(3, "localhost");
std::string out;
c.appendToReadBuffer("com");
check(!c.extractCommand(out), "fragmento incompleto nao vira comando");
c.appendToReadBuffer("man");
check(!c.extractCommand(out), "ainda incompleto");
c.appendToReadBuffer("d\r\n");
check(c.extractCommand(out) && out == "command", "remontou o comando");
```

Testar também: duas linhas num único append viram dois comandos; `\n` sozinho
(o `nc` manda assim) funciona igual a `\r\n`.

---

### Fase 2 — Servidor vivo (laço de `poll()` juntos, resto do Eduardo)

O laço de `poll()` numa sessão conjunta. Depois: `accept`, `recv` para o buffer
do cliente certo, drenar linhas completas, despachar pelo `CommandTable`,
`send` não bloqueante com `POLLOUT`, desconexão limpa.

**Pronto quando** o servidor aceita várias conexões ao mesmo tempo sem travar,
e o teste do subject passa de verdade:

```sh
./ircserv 6667 secret &

# terminal 1 — pacote parcial, Ctrl+D entre os pedaços
nc -C 127.0.0.1 6667
com^Dman^Dd

# terminal 2 — outro cliente ao mesmo tempo, o servidor não pode engasgar
nc -C 127.0.0.1 6667
```

Registro funcionando ponta a ponta:

```sh
printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n' | nc -C 127.0.0.1 6667
# tem que voltar 001, 002, 003, 004
```

E os caminhos de erro: senha errada → `464` e desconecta; `NICK` repetido →
`433`; comando antes do registro → `451`.

---

### Fase 3 — Convergência com o cliente real (fatia por fatia)

Aqui as duas trilhas se encontram. Cada comando é uma fatia: implementa, testa
contra o irssi, faz o PR, passa para a próxima. **Na ordem:**

1. Registrar no irssi (`/connect localhost 6667 secret`)
2. `JOIN` e `PRIVMSG` (canal e privado)
3. `PART`, `QUIT`, `PING`/`PONG`
4. Comandos de operador: `KICK`, `INVITE`, `TOPIC`
5. `MODE` — um flag de cada vez: `i`, `t`, `k`, `o`, `l`

**Pronto quando**, para cada fatia, dois clientes irssi de verdade fazem a
coisa e a outra ponta vê o resultado — não só o `nc`. O `nc` mostra os bytes; o
irssi mostra se o protocolo está certo.

```sh
# dois clientes, dois terminais
irssi -c localhost -p 6667 -w secret -n alice
irssi -c localhost -p 6667 -w secret -n bob
```

Atenção ao que o irssi faz de verdade (está detalhado na seção 7 do
`ARCHITECTURE.md`): manda `CAP LS 302` antes de tudo, manda `PASS`/`NICK`/`USER`
grudados num pacote só, e espera `PONG` nos `PING` dele.

---

### Fase 4 — Endurecimento e ensaio da avaliação (juntos)

- Pacotes parciais e pacotes colados, de novo, com carga.
- Casos de borda: canal vazio some, `KICK` no último operador, `MODE +k` sem
  parâmetro, `PRIVMSG` para nick inexistente, cliente que some sem `QUIT`.
- Regra do "nunca quebra": `Ctrl+C` no cliente, `kill -9` no cliente, mandar
  512+ bytes numa linha, mandar lixo binário, conectar e fechar na hora.
- `valgrind --leak-check=full ./ircserv 6667 secret` sem vazamento.
- **Ensaio da avaliação:** cada um explica a metade do outro em voz alta. Se
  travar em alguma parte, essa parte volta para a lista.

**Pronto quando** nenhum dos testes acima derruba o servidor e os dois
conseguem explicar qualquer arquivo do repo.

---

## 4. Como trabalhamos com sessões de IA separadas

Cada um está numa sessão de IA que **não enxerga as decisões do outro**. Os
`.md` são o cérebro compartilhado — em especial `ARCHITECTURE.md` (o contrato)
e `TASKS.md` (o estado do dia).

Regras práticas:

- Antes de pedir código para a IA, cole o trecho relevante do
  `ARCHITECTURE.md` na sessão. Sem isso ela inventa uma arquitetura diferente
  da do colega.
- Atualize o `TASKS.md` **no mesmo commit** que muda o código. É o que evita as
  duas sessões divergirem.
- Peça **uma fatia coerente** de cada vez (um comportamento observável, não um
  método solto), entenda, teste, e só então peça a próxima.
- Use IA para explicar conceito, revisar código nosso e gerar script de teste
  descartável. **Não** para gerar arquivo inteiro que a gente cola sem ler — na
  avaliação a gente precisa explicar tudo.

Desconfie ativamente de duas coisas:

1. **Numerics do IRC.** A IA inventa código e reescreve o texto da resposta com
   toda a confiança do mundo. Confira na tabela do `ARCHITECTURE.md` e no
   irssi.
2. **C++98.** Ela escorrega para C++11 o tempo todo (`auto`, `nullptr`,
   `std::to_string`, range-for). O compilador pega, mas leia antes de aceitar.

---

## 5. Git

Simples de propósito: sem gitflow, sem CI.

- Um repositório. A `main` **sempre compila e roda**.
- Branches curtas por fatia: `feat/transport-buffer`, `feat/channel-model`,
  `feat/cmd-join`…
- Merge sempre por **PR**, mesmo sendo dois. O PR não é burocracia: é o
  mecanismo de alfabetização. Quem revisa precisa entender o que aprovou,
  porque na avaliação pode ser cobrado exatamente daquilo.
- Mensagens de commit em inglês, no imperativo: `add channel key mode`.
- `.gitignore` já cobre `obj/`, `ircserv`, `run_tests`.

**Checklist de revisão de PR:**

- [ ] Compila com `-Wall -Wextra -Werror -std=c++98`, sem warning
- [ ] Nada de C++11
- [ ] Nenhuma lógica tocando em fd fora do `Server`
- [ ] Numerics conferidos contra a tabela do `ARCHITECTURE.md`
- [ ] `make test` passa
- [ ] `TASKS.md` atualizado
- [ ] Eu entendi o que estou aprovando e sei explicar
