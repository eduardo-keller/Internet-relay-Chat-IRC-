# Guia completo da avaliação do `ft_irc`

Este roteiro segue a mesma ordem e os mesmos subtítulos de
`docs/ft_irc_evaluation.md`. Ele foi escrito para esta implementação, cujo
executável é `ircserv`, cuja senha de exemplo é `secret` e cujo cliente IRC de
referência é o **irssi**.

> Execute tudo a partir da raiz do repositório clonado pelo avaliador. Durante
> a avaliação, não altere fontes, headers, `Makefile`, `README.md` ou scripts.
> Os comandos abaixo apenas compilam, executam ou inspecionam o projeto.

## Convenções e preparação dos terminais

Use a porta `6667` e a senha `secret` em todo o roteiro. Se a porta estiver
ocupada, escolha outra porta livre entre `1024` e `65535` e substitua `6667`
em todos os comandos.

Antes de iniciar:

```sh
pwd
ss -ltnp 'sport = :6667'
```

Abra **cinco terminais** na raiz do repositório:

| Terminal | Uso principal |
|---|---|
| **T1** | compilação, servidor e depois Valgrind |
| **T2** | irssi da operadora `alice` |
| **T3** | irssi do usuário normal `bob` |
| **T4** | `nc`, normalmente com `carol`, e pacotes parciais |
| **T5** | inspeção, monitoramento, flood controlado e eventualmente `dave` |

Para voltar ao shell do irssi sem fechar a conexão, pressione `Ctrl+Z`; use
`fg` para retomá-lo. Para sair normalmente do irssi, use `/quit motivo`. Para
encerrar um `nc`, pressione `Ctrl+C`.

Resultados IRC importantes que aparecerão nos testes:

| Código | Significado |
|---:|---|
| `001`–`004` | registro concluído |
| `324` | modos atuais do canal |
| `331` / `332` | canal sem tópico / tópico atual |
| `353` / `366` | lista de membros / fim da lista |
| `401` | nick inexistente |
| `403` | canal inexistente |
| `404` | não pode enviar ao canal |
| `411` / `412` | destinatário ausente / texto ausente |
| `431`–`433` | erro de nickname |
| `441` | usuário não está no canal |
| `442` | remetente não está no canal |
| `461` | parâmetros insuficientes |
| `464` | senha incorreta |
| `471` / `473` / `475` | limite / somente convite / chave incorreta |
| `482` | não é operador do canal |

# Introdução

Conduza a avaliação de forma educada, construtiva e honesta. Quando um
comportamento for ambíguo, mostre o comando, o resultado observado e o trecho
de código antes de discutir a interpretação. Falha funcional, crash e
trapaça devem ser sinalizados conforme as flags da ferramenta de avaliação;
fora o caso de trapaça, vale revisar o erro com a equipe mesmo quando a régua
encerra a avaliação.

Antes dos testes, todos devem ter lido o subject. Uma resposta curta sobre o
projeto pode ser:

> Este projeto implementa um servidor IRC em C++98. Ele aceita vários clientes
> TCP/IP sem `fork`, usa descritores não bloqueantes e um único `poll()` para
> multiplexar o socket de escuta e os clientes. O cliente de referência é o
> irssi. O servidor implementa registro com `PASS`, `NICK` e `USER`, canais,
> mensagens privadas e os comandos de operador `KICK`, `INVITE`, `TOPIC` e
> `MODE` com os modos `i`, `t`, `k`, `o` e `l`.

Para mostrar a visão geral do código:

```sh
sed -n '1,220p' README.md
sed -n '1,180p' include/Server.hpp
sed -n '15,60p' src/CommandTable.cpp
```

# Guidelines

## Confirmar o repositório e o clone

O avaliador deve clonar o repositório oficial em um diretório vazio. No clone
usado na avaliação, execute:

```sh
IRC_EVAL_DIR=$(mktemp -d /tmp/ft_irc-eval.XXXXXX)
git clone <URL_OFICIAL_DO_REPOSITORIO> "$IRC_EVAL_DIR"
cd "$IRC_EVAL_DIR"
```

Substitua o texto entre `<...>` pela URL mostrada na Intra. Não execute uma
URL copiada de fonte não conferida. Em seguida:

```sh
git remote -v
git status --short
git log -5 --oneline --decorate
git ls-files | sort
```

Confirme os logins dos integrantes, a URL oficial e que o projeto esperado é
o `ft_irc`. Em um clone recém-criado, `git status --short` deve ficar vazio
antes da compilação.

## Verificar aliases e comandos usados

```sh
type git make c++ nc irssi valgrind
alias
```

O resultado deve apontar para comandos reais, não aliases ou funções que
substituam silenciosamente `git`, `make`, compilador ou ferramentas de teste.

## Revisar scripts antes de usá-los

Os testes automáticos são auxiliares, não substituem a avaliação manual. Antes
de rodá-los, liste e leia os scripts:

```sh
find tests/it -maxdepth 1 -type f -name '*.sh' -print | sort
less tests/it/full_session.sh
less tests/it/hardening.sh
less tests/it/read_path.sh
less tests/it/write_path.sh
```

Saia do `less` com `q`. Se o avaliador não concordar com um script, não o
execute; faça o caso manual equivalente descrito neste guia.

## Regra de falha fatal

Durante toda a defesa, mantenha **T1 visível**. Qualquer segfault, abort,
terminação inesperada ou travamento do servidor encerra a avaliação com zero.
Depois de cada situação agressiva, confirme no T5:

```sh
pgrep -af ircserv
ss -ltnp 'sport = :6667'
```

## Anexos

A régua aponta para o subject oficial e para um arquivo auxiliar `bircd`. Para
esta defesa, o conteúdo obrigatório também está transcrito em
`docs/ft_irc_requirements.md`. Antes de avaliar, abra o subject fornecido na
Intra e compare-o com esse documento:

```sh
sed -n '1,320p' docs/ft_irc_requirements.md
```

O `bircd.tar.gz` é material auxiliar da avaliação, não uma dependência do
projeto e não deve ser incorporado ao código ou usado para substituir testes
do executável entregue.

# Parte Obrigatória

## README e Verificação de Conformidade

No T5:

```sh
head -n 1 README.md
rg -n '^## (Descrição|Instruções|Recursos)$' README.md
sed -n '1,260p' README.md
```

O que deve ser verificado:

1. A primeira linha é exatamente, em itálico:

   ```text
   *Este projeto foi criado como parte do currículo da 42 por brnascim, ekeller-.*
   ```

2. `## Descrição` explica o servidor, protocolo, arquitetura e funcionalidades.
3. `## Instruções` mostra pré-requisitos, `make`, execução de `ircserv`, irssi,
   `nc` e testes.
4. `## Recursos` lista RFCs, `poll`, networking, irssi e descreve onde IA foi
   usada.

Resposta sugerida:

> O README contém os quatro elementos pedidos. A primeira linha traz os dois
> logins no formato exato; Descrição apresenta o servidor e sua arquitetura;
> Instruções cobre compilação e execução; Recursos lista as referências e
> explica as tarefas apoiadas por IA e a revisão humana final.

Se qualquer elemento estiver ausente no repositório oficialmente entregue, a
régua determina nota zero.

> **Confirme o tamanho real do grupo na Intra:** o cabeçalho da régua recebida
> diz que a equipe tem 3 estudantes, enquanto a primeira linha atual do README
> lista `brnascim` e `ekeller-`. Se o grupo oficial realmente tiver um terceiro
> integrante, o login dele precisa constar nessa primeira linha; se o grupo
> oficial tiver apenas dois, explique que o número 3 veio do cabeçalho/template
> da régua. Não invente um login apenas para coincidir com o template.

## Verificações básicas

### 1. Existe um `Makefile`

No T1:

```sh
ls -l Makefile
sed -n '1,220p' Makefile
```

Mostre os alvos `all`, `clean`, `fclean`, `re` e o alvo do executável
`$(NAME)`, com `NAME := ircserv`.

### 2. Compilação, flags e relink desnecessário

Ainda no T1:

```sh
make fclean
make
make
```

Resultados esperados:

- o primeiro `make` termina sem warnings ou erros e cria `./ircserv`;
- a linha de compilação contém `c++ -Wall -Wextra -Werror -std=c++98`;
- o segundo `make` responde `Nothing to be done for 'all'` e não relinka.

Verificações adicionais:

```sh
test -x ./ircserv && echo 'ircserv executável: OK'
file ./ircserv
find src include -type f \( -name '*.cpp' -o -name '*.hpp' \) -print | sort
rg -n '^(NAME|CXX|CXXFLAGS)|^(all|clean|fclean|re):' Makefile
```

Resposta sugerida:

> O projeto é C++98, compilado por `c++` com `-Wall -Wextra -Werror
> -std=c++98`. O binário produzido se chama `ircserv`; dependências `.d`
> evitam under-build e o Makefile não relinka quando nada mudou.

### 3. Quantas chamadas `poll()` existem?

No T5:

```sh
rg -n 'poll\s*\(' src include --glob '*.{cpp,hpp}'
nl -ba src/Server.cpp | sed -n '166,242p'
```

Os comentários também contêm a palavra `poll`, mas há **uma única chamada
executável**, em `Server::run()`:

```cpp
int timeout = hasLingeringClients() ? irc::LINGER_POLL_MS : -1;
int ready = poll(&_pollFds[0], _pollFds.size(), timeout);
```

Resposta sugerida:

> Existe exatamente uma chamada a `poll()`, em `Server::run()`. Antes dela,
> `buildPollFds()` cria um vetor contendo o socket de escuta e todos os clientes.
> O mesmo retorno de `poll()` governa `accept`, leitura e escrita. O timeout é
> `-1` durante operação normal e passa a `LINGER_POLL_MS` somente enquanto um
> cliente marcado para desconexão aguarda a fila de saída esvaziar.

Para mostrar como os eventos são montados:

```sh
nl -ba src/Server.cpp | sed -n '262,350p'
```

Explique que o socket de escuta e os clientes ativos têm `POLLIN`; `POLLOUT` só
é armado quando aquele cliente possui saída pendente. Um cliente já marcado
para desconexão deixa de receber `POLLIN` e permanece apenas com `POLLOUT`
enquanto houver algo a enviar. Isso evita novos comandos depois do `QUIT` e
evita busy loop.

### 4. `accept`, `recv` e `send` em relação ao `poll()`

Localize as syscalls reais:

```sh
rg -n '\b(accept|recv|send)\s*\(' src --glob '*.cpp'
nl -ba src/Server.cpp | sed -n '179,257p'
nl -ba src/Server.cpp | sed -n '308,453p'
nl -ba src/Server.cpp | sed -n '543,707p'
```

Resposta sugerida:

> O `accept()` só é chamado quando o único `poll()` marcou `POLLIN` no socket
> de escuta. O `recv()` só é chamado por `handleReadable()` após `POLLIN`. O
> único `send()` do projeto ocorre em `handleWritable()` após `POLLOUT`. Cada
> evento executa no máximo uma syscall; dados restantes aguardam o próximo
> `poll()`.

Mostre especificamente:

- `Server::run()`, em `src/Server.cpp`: tradução de `revents` para handlers;
- `Server::acceptNewClient()`: um `accept()` por prontidão;
- `Server::handleReadable()`: um `recv()` por prontidão;
- `Server::handleWritable()`: um `send()` por prontidão;
- `Client::getOutputBuffer()` e `consumeOutput()`, em `src/Client.cpp`: suporte
  a `send()` parcial.

#### Desconexão sem `send()` fora de `POLLOUT`

Confirme que `reapDisconnected()` não chama mais `send()`:

```sh
nl -ba src/Server.cpp | sed -n '634,707p'
nl -ba include/Limits.hpp | sed -n '38,57p'
nl -ba src/Client.cpp | sed -n '462,499p'
```

Resposta sugerida:

> `disconnectClient()` apenas marca o cliente e enfileira a resposta. Se ainda
> existe saída, `buildPollFds()` mantém esse fd somente com `POLLOUT`, e o envio
> acontece pelo mesmo `handleWritable()` usado por qualquer cliente. O reaper
> apenas espera a fila esvaziar ou o limite de três passagens expirar; ele não
> escreve no socket. Um timeout de 200 ms, usado apenas durante o linger, impede
> que um peer que nunca fica gravável retenha o fd indefinidamente.

Depois de revisar o script, a validação automática específica desse caminho é:

```sh
./tests/it/write_path.sh 6792
```

Na última atualização deste guia, o resultado foi **5 passed, 0 failed**,
incluindo a chegada de `ERROR` antes do fechamento e CPU ociosa sem busy loop.

### 5. Uso de `errno` após as syscalls

```sh
rg -n 'errno|EAGAIN|EWOULDBLOCK|EINTR' src/Server.cpp
nl -ba src/Server.cpp | sed -n '194,206p;319,339p;404,427p;577,601p'
```

Resposta exata sobre esta implementação:

> `accept()`, `recv()` e `send()` não consultam `errno` e nunca são repetidos
> dentro do handler. `accept()` retorna quando falha. Depois de `recv()`, o
> servidor decide apenas pelo número de bytes: `n <= 0` desconecta. Depois de
> `send()`, `n < 0` marca erro de escrita e um valor positivo consome exatamente
> os bytes enviados. O próximo acesso ao fd depende de um novo evento de
> `poll()`.

O `rg` ainda encontra `errno` no tratamento de falhas de inicialização e após
o próprio `poll()`. Isso não contradiz o quesito: a proibição desta parte da
régua é usar `errno` para dirigir comportamento depois de `accept`, `recv` ou
`send`. Mostre que não há referência a `errno` nos respectivos handlers.

### 6. Uso permitido de `fcntl()`

```sh
rg -n '\bfcntl\s*\(' src include --glob '*.{cpp,hpp}'
nl -ba src/Server.cpp | sed -n '129,164p;312,340p'
```

Há duas chamadas executáveis, ambas exatamente no formato permitido:

```cpp
fcntl(_listenFd, F_SETFL, O_NONBLOCK);
fcntl(fd, F_SETFL, O_NONBLOCK);
```

Resposta sugerida:

> Uma chamada torna o socket de escuta não bloqueante; a outra torna cada
> socket retornado por `accept()` não bloqueante. Não há outro comando ou flag
> de `fcntl()` no projeto.

### 7. Sem `fork` e toda E/S não bloqueante

```sh
rg -n '\bfork\s*\(' src include --glob '*.{cpp,hpp}' || echo 'fork ausente: OK'
```

Mostre também `setupListenSocket()`, `acceptNewClient()` e `buildPollFds()`.
Explique que não existe thread ou processo por cliente: todos os fds pertencem
ao mesmo `Server` e são multiplexados pelo vetor `_pollFds`.

### 8. Testes unitários auxiliares

Não fazem parte da régua, mas são uma verificação rápida antes da rede:

```sh
make test
```

No estado analisado durante a última atualização deste guia, o resultado foi **609 passed,
0 failed**. Na defesa, use o resultado obtido no clone oficial, não apenas este
número documentado.

## Networking

### 1. Iniciar o servidor

No **T1**:

```sh
./ircserv 6667 secret
```

Resultado esperado:

```text
ircserv: listening on port 6667
```

Mantenha T1 aberto durante todos os testes. Para encerramento limpo, pressione
`Ctrl+C`; o esperado é `ircserv: shutting down`.

### 2. Confirmar escuta em todas as interfaces

No **T5**, com o servidor ativo:

```sh
ss -ltnp 'sport = :6667'
lsof -nP -iTCP:6667 -sTCP:LISTEN
ip -4 -brief address
```

O endereço local deve aparecer como `0.0.0.0:6667`, e não apenas
`127.0.0.1:6667`. Opcionalmente, conecte o `nc` usando um dos endereços não
loopback mostrados por `ip` para provar o bind fora de localhost. Para mostrar
no código:

```sh
nl -ba src/Server.cpp | sed -n '129,164p'
```

Resposta sugerida:

> O projeto usa IPv4/TCP. `addr.sin_addr.s_addr = htonl(INADDR_ANY)` faz o
> `bind()` em todas as interfaces, e `htons(_port)` usa exatamente a porta
> recebida na linha de comando.

### 3. Conectar e obter resposta com `nc`

No **T4**:

```sh
nc -C 127.0.0.1 6667
```

Digite, uma linha de cada vez:

```text
PING :teste-nc
PASS secret
NICK carol
USER carol 0 * :Carol Netcat
JOIN #avaliacao
```

Resultados esperados:

- `PING` responde com `:ircserv PONG ircserv :teste-nc` mesmo antes do registro;
- após `USER`, chegam os numéricos `001`, `002`, `003` e `004`;
- `JOIN` retorna o eco do join, `331`, `353` e `366`;
- no `353`, o primeiro criador do canal aparece com `@`.

Se `carol` for a cliente auxiliar e não deve criar o canal, deixe apenas o
registro pronto e faça Alice criar o canal primeiro no passo seguinte.

### 4. Cliente IRC de referência e conexão com irssi

Pergunta da régua:

> Qual é o cliente IRC de referência?

Resposta:

> O cliente de referência é o **irssi**. Ele está declarado no README e o
> servidor aceita os comandos auxiliares `CAP`, `PING`, `PONG`, `WHO`, `WHOIS`,
> `QUIT` e consultas de `MODE` que o irssi envia durante uma sessão real.

Encerre o `nc` do T4 com `Ctrl+C` se ele já estiver usando `carol` e então
inicie os clientes.

No **T2**:

```sh
irssi -c 127.0.0.1 -p 6667 -w secret -n alice
```

Dentro do irssi de Alice:

```text
/join #avaliacao
```

No **T3**:

```sh
irssi -c 127.0.0.1 -p 6667 -w secret -n bob
```

Dentro do irssi de Bob:

```text
/join #avaliacao
```

O `@` ao lado de `alice` mostra que a criadora é operadora; `bob` não deve ter
`@`. Se o irssi já possui configuração que altera nick ou autojoin, use um
perfil limpo ou confirme os nicks com `/nick alice` e `/nick bob`.

### 5. Várias conexões simultâneas e servidor responsivo

Deixe Alice e Bob conectados. Abra novamente no **T4**:

```sh
nc -C 127.0.0.1 6667
```

Registre um terceiro cliente:

```text
PASS secret
NICK carol
USER carol 0 * :Carol
JOIN #avaliacao
PING :tres-clientes
```

No **T2**, digite como mensagem normal na janela de `#avaliacao`:

```text
mensagem da alice para todos
```

Bob no T3 e Carol no T4 devem receber uma linha equivalente a:

```text
:alice!<user>@127.0.0.1 PRIVMSG #avaliacao :mensagem da alice para todos
```

No T4:

```text
PRIVMSG #avaliacao :resposta do nc com espacos
```

Alice e Bob devem receber a mensagem. O remetente não recebe eco protocolar do
próprio `PRIVMSG`, pois o cliente já mostra localmente o que ele digitou.

Enquanto os três clientes permanecem conectados, no T5:

```sh
pgrep -af ircserv
ps -o pid,stat,%cpu,%mem,cmd -C ircserv
```

O processo deve continuar vivo, sem bloqueio e com CPU ociosa próxima de zero
quando ninguém está enviando dados.

### 6. Onde mostrar o networking no código

```sh
nl -ba src/Server.cpp | sed -n '129,350p'
nl -ba src/Server.cpp | sed -n '392,453p'
nl -ba src/Server.cpp | sed -n '543,601p'
nl -ba src/ServerChannels.cpp | sed -n '66,131p'
nl -ba src/CommandsChannel.cpp | sed -n '425,484p'
```

Explique:

- `setupListenSocket()` cria, configura, faz `bind` e `listen`;
- `run()` atende todos os clientes no mesmo loop;
- `handleReadable()` recebe bytes e extrai todas as linhas completas;
- `sendToClient()` enfileira saída e `handleWritable()` consome apenas os bytes
  efetivamente enviados;
- `broadcastToChannel()` percorre os membros;
- `cmdPrivmsg()` exclui o remetente do broadcast do canal.

## Situações especiais de rede

Antes de começar, mantenha Alice e Bob registrados e no canal `#avaliacao`.
Use o T4 para cada conexão `nc`; quando um caso terminar, feche aquele `nc` e
abra outro.

### 1. Comando enviado em partes, como no subject

No **T4**:

```sh
nc -C 127.0.0.1 6667
```

Faça literalmente esta sequência:

1. digite `com` e pressione `Ctrl+D` uma vez;
2. digite `man` e pressione `Ctrl+D` uma vez;
3. digite `d` e pressione `Enter`.

Não pressione `Ctrl+D` numa linha vazia, pois isso encerraria a entrada do
`nc`. O servidor deve reconstruir **um único** comando `command` e responder
uma única vez:

```text
:ircserv 421 * COMMAND :Unknown command
```

Onde mostrar:

```sh
nl -ba src/Server.cpp | sed -n '392,453p'
nl -ba src/Client.cpp | sed -n '245,375p'
```

Resposta sugerida:

> TCP é um fluxo de bytes, não um fluxo de mensagens. Cada `Client` tem seu
> próprio `_readBuffer`. `appendToReadBuffer()` acumula fragmentos e
> `extractCommand()` só entrega uma linha quando encontra `\n`, removendo o
> `\r` final quando presente. O restante incompleto permanece para o próximo
> evento de leitura.

### 2. Um parcial não pode bloquear outras conexões

Abra um novo `nc` no **T4** e digite `PING :par`, seguido de `Ctrl+D`, sem
Enter. O servidor ainda não deve responder porque a linha está incompleta.

Enquanto isso:

- no T2, Alice envia `servidor continua responsivo` em `#avaliacao`;
- Bob deve receber imediatamente no T3;
- no T5, confirme que o servidor está vivo com `pgrep -af ircserv`.

Volte ao T4, digite `cial` e pressione Enter. O comando reconstruído é
`PING :parcial`, e a resposta deve ser:

```text
:ircserv PONG ircserv :parcial
```

### 3. Encerrar inesperadamente um cliente IRC

Feche a conexão parcial do passo anterior. No T4, abra outro `nc`, registre
Carol e entre em `#avaliacao`:

```sh
nc -C 127.0.0.1 6667
```

```text
PASS secret
NICK carol
USER carol 0 * :Carol
JOIN #avaliacao
```

Para obter o PID exato de Bob, a maneira mais segura é reiniciar o T3 assim:

Primeiro saia da instância atual de Bob com `/quit preparando-teste-kill`,
volte ao shell e execute:

```sh
echo "PID que será substituído pelo irssi: $$"
exec irssi -c 127.0.0.1 -p 6667 -w secret -n bob
```

Entre novamente em `#avaliacao`. Anote o PID mostrado. No **T5**, substitua
`<PID_BOB>` somente por esse número previamente conferido:

```sh
ps -fp <PID_BOB>
kill -9 <PID_BOB>
```

Esse é um encerramento inesperado, sem `QUIT`. Verifique:

1. T1 registra o fechamento do fd, mas `ircserv` permanece ativo;
2. Alice e Carol continuam trocando mensagens;
3. abra um novo T3, reconecte Bob e faça `/join #avaliacao` novamente.

No código, mostre `SIGPIPE` ignorado, `POLLHUP`/`recv == 0`, desconexão adiada
e limpeza dos canais:

```sh
nl -ba src/Server.cpp | sed -n '109,127p;225,257p;404,453p;605,707p'
nl -ba src/ServerChannels.cpp | sed -n '133,180p'
```

### 4. Encerrar `nc` com metade de um comando

No T4, conecte um novo `nc`, registre `carol`, entre no canal e então digite:

```text
PRIVMSG #avaliacao :esta mensagem ficou pela metade
```

Não pressione Enter. Pressione `Ctrl+D` uma vez para enviar o fragmento e logo
depois `Ctrl+C` para matar o `nc`. A linha parcial não deve ser executada. No
T2 e T3, envie novas mensagens e confirme que o servidor continua normal.
Conecte ainda outro `nc` e envie `PING :depois-da-metade` para provar que um
novo cliente também é aceito.

### 5. Suspender cliente, inundar canal e retomar

Garanta que Bob está conectado ao `#avaliacao` no T3. Pressione `Ctrl+Z` no
**T3**. O shell deve mostrar que o irssi foi suspenso.

No **T5**, envie um flood controlado de 200 mensagens por uma conexão
transitória:

```sh
(
    printf 'PASS secret\r\nNICK flooder\r\nUSER flooder 0 * :Flooder\r\n'
    printf 'JOIN #avaliacao\r\n'
    for i in $(seq -w 1 200); do
        printf 'PRIVMSG #avaliacao :flood-%s\r\n' "$i"
    done
    sleep 2
) | nc -C 127.0.0.1 6667
```

Durante o flood, Alice deve continuar responsiva. Em outro `nc` no T4:

```text
PING :durante-flood
```

O PONG deve chegar sem o servidor congelar. No T3, retome Bob:

```sh
fg
```

O irssi deve processar normalmente as mensagens acumuladas. Confira o começo
e o fim com:

```text
/lastlog flood-001
/lastlog flood-200
```

Não aumente o flood indefinidamente: esta implementação limita a fila por
cliente a `65536` bytes e desconecta deliberadamente um cliente que excede
essa fila com `ERROR :SendQ exceeded`. Esse limite protege o servidor contra
exaustão de memória; o caso da régua deve exercitar suspensão e recuperação,
não exceder propositalmente a SendQ.

Onde mostrar o tratamento de cliente lento:

```sh
nl -ba include/Limits.hpp | sed -n '18,70p'
nl -ba src/Client.cpp | sed -n '381,499p'
nl -ba src/Server.cpp | sed -n '269,305p;543,601p;634,707p'
```

Explique que a fila é individual, `POLLOUT` só é armado quando necessário,
`send()` parcial preserva o restante e uma conexão lenta não bloqueia as outras.
Se o cliente já está sendo desconectado, o linger é limitado a três passagens;
o timeout de 200 ms faz esse orçamento avançar mesmo se não houver `POLLOUT`.

### 6. Vazamentos de memória durante a operação

Saia dos clientes com `/quit`, encerre o servidor normal com `Ctrl+C` e inicie
uma nova execução no **T1** sob Valgrind:

```sh
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
    --error-exitcode=42 --log-file=/tmp/ft_irc_valgrind.log \
    ./ircserv 6667 secret
```

Reconecte Alice e Bob nos T2/T3, entre em `#avaliacao`, repita a suspensão,
flood controlado e `fg`. Depois:

1. saia de Alice e Bob com `/quit fim-do-valgrind`;
2. encerre o servidor com `Ctrl+C` no T1;
3. inspecione o relatório no T5:

```sh
rg 'definitely lost|indirectly lost|possibly lost|still reachable|ERROR SUMMARY|FILE DESCRIPTORS' /tmp/ft_irc_valgrind.log
```

Resultado esperado: `definitely lost: 0 bytes`, `indirectly lost: 0 bytes` e
`ERROR SUMMARY: 0 errors`. Memória ainda alcançável da própria ferramenta ou
bibliotecas deve ser analisada, não simplesmente ignorada. O socket de escuta
e os clientes devem ser fechados no encerramento.

Teste automatizado equivalente, somente após revisão do script:

```sh
make
./tests/it/full_session.sh 6719
```

Esse script utiliza outra porta, executa uma sessão completa sob Valgrind e
espera zero erros, zero `Invalid read/write/free` e nenhum bloco definitivamente
ou indiretamente perdido.

## Comandos básicos do cliente

Reinicie o servidor normal no T1 caso ele tenha sido encerrado pelo Valgrind:

```sh
./ircserv 6667 secret
```

### 1. Autenticação, nickname, username e canal com `nc`

No T4:

```sh
nc -C 127.0.0.1 6667
```

Digite exatamente:

```text
PASS secret
NICK carol
USER carol 0 * :Carol da Avaliacao
JOIN #basico
```

Confirme `001`–`004`, depois o `JOIN`, `331` ou `332`, `353` e `366`.

Casos de erro úteis:

1. uma conexão nova com `PASS errada` deve receber `464` e ser fechada;
2. `NICK` antes de `PASS` deve receber `451`;
3. um segundo cliente tentando `NICK carol` deve receber `433`.

Onde mostrar:

```sh
nl -ba src/CommandsRegistration.cpp | sed -n '56,214p'
nl -ba src/Client.cpp | sed -n '109,225p'
nl -ba src/CommandsChannel.cpp | sed -n '84,259p'
```

### 2. Autenticação e canal com irssi

No T2 e T3, inicie Alice e Bob como explicado anteriormente. O próprio irssi
envia `PASS`, `NICK` e `USER`; o recebimento de `001` conclui a conexão. Faça:

```text
/join #basico
```

em ambos. A lista de nicks deve mostrar Alice e Bob e apenas a criadora com
`@`.

### 3. `PRIVMSG` direto e texto com vários parâmetros

Com Alice e Bob conectados, no irssi de Alice:

```text
/msg bob mensagem privada com varios espacos e : dois-pontos
/quote PRIVMSG bob,carol :mensagem para dois destinatarios
```

Bob deve abrir/atualizar a query e receber o primeiro texto inteiro. Na segunda
linha, Bob e Carol recebem a mesma mensagem, cada um com o próprio nick como
destino. Pelo protocolo cru no T4:

```text
PRIVMSG bob :mensagem privada do nc com varios parametros
PRIVMSG #basico :texto de canal com espacos : e dois-pontos
PRIVMSG alice,bob :mensagem do nc para uma lista de nicks
```

O parâmetro iniciado por `:` é o trailing parameter: tudo depois dele,
incluindo espaços e outros `:`, deve chegar intacto. A lista antes dele é
separada por vírgulas; cada alvo válido é processado independentemente, então
um alvo inválido não deve impedir a entrega aos demais.

Teste também respostas de erro, uma linha por vez:

```text
PRIVMSG
PRIVMSG bob
PRIVMSG ninguem :teste
```

Resultados esperados: `411`, `412` e `401`, respectivamente. O servidor deve
permanecer conectado após esses erros.

Onde mostrar:

```sh
nl -ba src/Message.cpp | sed -n '1,140p'
nl -ba src/CommandsChannel.cpp | sed -n '360,484p'
nl -ba src/ServerChannels.cpp | sed -n '66,89p'
```

Resposta sugerida:

> `parseMessage()` preserva o trailing parameter como um único parâmetro,
> então espaços e dois-pontos internos não são perdidos. `cmdPrivmsg()` separa
> a lista de destinos e processa cada canal ou nick independentemente, valida
> erros e usa o prefixo `nick!user@host`. Para canal,
> `broadcastToChannel()` envia a todos os outros membros; para nick,
> `findClientByNick()` faz busca case-insensitive.

## Comandos de cliente operador de canal

Esta seção deve começar com estado limpo. Saia dos clientes ainda abertos com
`/quit`, feche os `nc` com `Ctrl+C`, encerre o servidor no T1 com `Ctrl+C` e
inicie outra vez:

```sh
./ircserv 6667 secret
```

Reconecte Alice no T2 e Bob no T3, um comando em cada terminal:

```sh
irssi -c 127.0.0.1 -p 6667 -w secret -n alice
```

```sh
irssi -c 127.0.0.1 -p 6667 -w secret -n bob
```

Use:

- **T2:** `alice`, criadora e operadora de `#ops`;
- **T3:** `bob`, membro normal de `#ops`;
- **T4:** `carol`, registrada por `nc`, inicialmente fora de `#ops`;
- **T5:** `dave`, por `nc`, somente quando o teste de limite pedir.

Sequência inicial:

No T2:

```text
/join #ops
```

No T3:

```text
/join #ops
```

No T4:

```sh
nc -C 127.0.0.1 6667
```

```text
PASS secret
NICK carol
USER carol 0 * :Carol
```

Use `/quote` no irssi para enviar exatamente a linha IRC mostrada, sem a
tradução de comandos amigáveis do cliente.

Para que o mesmo bloqueio seja demonstrado também com protocolo cru, coloque
Carol no canal antes das tentativas de usuário normal. No T4:

```text
JOIN #ops
```

### 1. Confirmar que usuário normal não tem privilégios

#### `KICK` negado

No T3 (Bob):

```text
/quote KICK #ops alice :tentativa sem permissao
```

Bob deve receber `482`; Alice continua no canal.

Repita pelo `nc` no T4:

```text
KICK #ops alice :tentativa do nc sem permissao
```

Carol também deve receber `482`.

#### `MODE` negado

No T3:

```text
/quote MODE #ops +i
```

Bob deve receber `482` e o modo não deve mudar.

No T4:

```text
MODE #ops +i
```

Carol também recebe `482`.

#### `INVITE` negado quando `+i` está ativo

No T2:

```text
/quote MODE #ops +i
```

No T3:

```text
/quote INVITE carol #ops
```

Bob deve receber `482`. Nesta implementação um membro normal pode convidar em
canal aberto; o privilégio de operador para `INVITE` é exigido quando `+i`
torna o convite uma decisão de acesso. Por isso o teste correto ativa `+i`
antes da tentativa do usuário normal.

No T4, Carol já é membro normal e tenta convidar um nick qualquer:

```text
INVITE ninguem #ops
```

O teste de privilégio ocorre antes da procura do nick, portanto o resultado
também deve ser `482`.

#### `TOPIC` negado quando `+t` está ativo

No T2:

```text
/quote MODE #ops +t
```

No T3:

```text
/quote TOPIC #ops :topico indevido do bob
```

Bob deve receber `482`, e uma consulta `TOPIC #ops` ainda mostra o tópico
anterior ou `331`. O modo `+t` restringe **alteração**, não consulta do tópico.

No T4:

```text
TOPIC #ops :topico indevido da carol
PART #ops :fim dos testes de usuario normal
```

Carol recebe `482` para `TOPIC` e depois sai do canal, ficando disponível como
alvo do teste de `INVITE` do operador.

### 2. `KICK` por operador

No T2:

```text
/quote KICK #ops bob :teste de KICK
```

Resultados esperados:

- Alice e Bob recebem a linha `KICK`;
- Bob sai de `#ops`, mas a conexão dele permanece ativa;
- Bob pode executar novamente `/join #ops`.

No T3:

```text
/join #ops
```

Onde mostrar:

```sh
nl -ba src/CommandsChannel.cpp | sed -n '559,675p'
```

Explique que o handler verifica existência, membership, operador e vítima;
transmite o KICK antes de remover a vítima e não desconecta o cliente.

### 3. `INVITE` por operador e modo `i`

Garanta `+i` no T2:

```text
/quote MODE #ops +i
/quote INVITE carol #ops
```

Alice deve receber `341`; Carol no T4 recebe uma linha `INVITE`. No T4:

```text
JOIN #ops
```

Carol entra. Depois retire Carol para testar novamente o gate:

```text
PART #ops :vou testar o invite-only
JOIN #ops
```

Sem novo convite, o segundo JOIN deve receber `473`. No T2, convide novamente;
no T4, repita `JOIN #ops` e confirme sucesso. Por fim, no T2:

```text
/quote MODE #ops -i
```

Onde mostrar:

```sh
nl -ba src/CommandsChannel.cpp | sed -n '84,128p;677,763p'
nl -ba include/Channel.hpp | sed -n '49,75p'
```

Explique que convites guardam a identidade `Client*`, sobrevivem a troca de
nick e são consumidos apenas após JOIN bem-sucedido.

### 4. `TOPIC` por operador e modo `t`

No T2:

```text
/quote MODE #ops +t
/quote TOPIC #ops :Topico definido pela operadora
```

Todos no canal recebem a alteração. No T3:

```text
/quote TOPIC #ops
/quote TOPIC #ops :Bob nao pode enquanto mais t
```

A consulta retorna `332`; a alteração retorna `482`. No T2:

```text
/quote MODE #ops -t
```

Agora, no T3:

```text
/quote TOPIC #ops :Bob pode depois de menos t
```

A alteração deve funcionar e ser transmitida a todos. Para limpar o tópico:

```text
/quote TOPIC #ops :
```

Onde mostrar:

```sh
nl -ba src/CommandsChannel.cpp | sed -n '486,557p'
```

### 5. `MODE i`: adicionar e remover invite-only

O comportamento completo já foi demonstrado no teste de `INVITE`. Resumo
repetível:

1. Alice envia `MODE #ops +i`;
2. Carol fora do canal envia `JOIN #ops` e recebe `473`;
3. Alice envia `INVITE carol #ops`;
4. Carol entra;
5. Alice envia `MODE #ops -i`;
6. depois de `PART`, Carol volta a entrar sem convite.

Todo `MODE` aplicado deve ser transmitido para os membros, incluindo Alice.

### 6. `MODE t`: adicionar e remover restrição de tópico

O comportamento completo foi demonstrado no teste de `TOPIC`:

- com `+t`, Bob recebe `482` e Alice altera o tópico;
- com `-t`, Bob também consegue alterá-lo.

### 7. `MODE k`: adicionar e remover chave do canal

Deixe Carol fora com `PART #ops`. No T2:

```text
/quote MODE #ops +k chave42
```

No T4:

```text
JOIN #ops
JOIN #ops errada
JOIN #ops chave42
```

Os dois primeiros JOINs recebem `475`; o terceiro entra. Retire Carol novamente
no T4:

```text
PART #ops :testando menos k
```

Remova a chave no T2:

```text
/quote MODE #ops -k
```

No T4:

```text
JOIN #ops
```

O JOIN sem chave funciona após `-k`. Note que esta implementação não exige o
valor antigo como parâmetro para remover `k`.

### 8. `MODE o`: promover e rebaixar operador

No T2:

```text
/quote MODE #ops +o bob
```

Bob deve ganhar `@`. No T3, prove o privilégio recém-concedido:

```text
/quote MODE #ops +t
```

O comando funciona. No T2:

```text
/quote MODE #ops -o bob
```

No T3:

```text
/quote MODE #ops -t
```

Agora Bob recebe `482`. Alice pode limpar `-t` no T2.

### 9. `MODE l`: adicionar e remover limite de usuários

Conte os membros de `#ops`. Supondo Alice, Bob e Carol dentro, fixe o limite
atual em 3. No T2:

```text
/quote MODE #ops +l 3
```

No **T5**, abra Dave:

```sh
nc -C 127.0.0.1 6667
```

```text
PASS secret
NICK dave
USER dave 0 * :Dave
JOIN #ops
```

Dave recebe `471`. No T2:

```text
/quote MODE #ops -l
```

No T5, Dave repete:

```text
JOIN #ops
```

Agora entra. Se o número inicial de membros for diferente, use como parâmetro
de `+l` exatamente a contagem atual, de forma que o próximo JOIN exceda o
limite.

### 10. Consultar e mostrar todos os modos no código

No T2:

```text
/quote MODE #ops
```

O servidor responde com `324` e os modos ativos. A chave é exibida apenas a
membros do canal.

Mostre a implementação completa:

```sh
nl -ba src/CommandsChannel.cpp | sed -n '765,1005p'
nl -ba src/Channel.cpp | sed -n '137,276p'
```

Resposta sugerida:

> `applyModeChanges()` primeiro exige que o remetente seja operador. Ele
> percorre a string de modos e consome parâmetros da esquerda para a direita:
> `+k`, `+l` e ambos os sinais de `o` recebem parâmetros; `-k`, `-l`, `i` e
> `t` não. O `Channel` armazena flags, chave, limite, operadores e convites. O
> resultado aplicado é transmitido ao canal, e `MODE #canal` responde `324`.

### 11. Repetir todos os comandos de operador pelo `nc`

A régua pede verificação com `nc` e com o cliente de referência. Os passos
anteriores já usaram o irssi como operador e o `nc` como usuário normal. Agora
faça Carol criar um canal separado pelo T4; por ser a primeira integrante, ela
será operadora:

No T4:

```text
JOIN #ncops
MODE #ncops +itkl chave-nc 2
TOPIC #ncops :Topico definido pelo nc
INVITE dave #ncops
```

Carol deve receber/transmitir os `MODE` e `TOPIC`, e receber `341` para o
convite. Dave está conectado no T5 desde o teste de `+l`. No T5:

```text
JOIN #ncops chave-nc
```

Dave entra porque foi convidado, forneceu a chave e há uma segunda vaga. No
T4, teste promoção e remoção de operador:

```text
MODE #ncops +o dave
MODE #ncops -o dave
```

No T5, Dave prova que voltou a ser usuário normal:

```text
MODE #ncops -i
```

O resultado deve ser `482`. No T4, Carol executa o KICK, convida Dave de novo
e por fim remove os modos:

```text
KICK #ncops dave :KICK enviado pelo nc operador
INVITE dave #ncops
MODE #ncops -itkl
MODE #ncops
```

Resultados esperados:

- `KICK`, `INVITE`, `TOPIC` e `MODE` foram emitidos diretamente pelo `nc`;
- os cinco modos `i`, `t`, `k`, `o` e `l` foram alterados pelo protocolo cru;
- Dave recebeu o KICK sem ser desconectado;
- a consulta final `MODE #ncops` retorna `324` com `+`, pois `-itkl` limpou os
  quatro modos persistentes e `-o` já havia removido o papel de Dave.

### 12. Critério final dos comandos de operador

Antes de concluir, confirme cada item:

- [ ] usuário normal recebeu `482` ao tentar `KICK`;
- [ ] usuário normal recebeu `482` ao tentar `MODE`;
- [ ] usuário normal recebeu `482` ao tentar `INVITE` sob `+i`;
- [ ] usuário normal recebeu `482` ao tentar alterar `TOPIC` sob `+t`;
- [ ] operador executou `KICK`;
- [ ] operador executou `INVITE`;
- [ ] `TOPIC` foi consultado, alterado e limpo;
- [ ] `MODE +i` e `-i` foram demonstrados;
- [ ] `MODE +t` e `-t` foram demonstrados;
- [ ] `MODE +k` e `-k` foram demonstrados;
- [ ] `MODE +o` e `-o` foram demonstrados;
- [ ] `MODE +l` e `-l` foram demonstrados.
- [ ] os comandos foram demonstrados pelo irssi e pelo `nc`.

A régua determina dedução de um ponto para cada funcionalidade de operador que
não funcionar.

# Encerramento da avaliação

Saia de cada irssi:

```text
/quit fim da avaliacao
```

Encerre cada `nc` com `Ctrl+C`. Por último, pressione `Ctrl+C` no T1. Confirme:

```sh
pgrep -af ircserv || echo 'servidor encerrado: OK'
```

Se a última execução foi sob Valgrind, confira novamente:

```sh
rg 'definitely lost|indirectly lost|ERROR SUMMARY' /tmp/ft_irc_valgrind.log
```

# Mapa rápido: pergunta do avaliador → resposta → código

| Pergunta | Resposta curta | Onde mostrar |
|---|---|---|
| Cliente de referência? | irssi | `README.md`, Descrição/Instruções |
| Quantos `poll()`? | um | `Server::run()` em `src/Server.cpp` |
| Como escuta todas as interfaces? | `INADDR_ANY` | `Server::setupListenSocket()` |
| Como evita bloqueio? | `O_NONBLOCK`, um evento/uma syscall e filas por cliente | `setupListenSocket()`, `acceptNewClient()`, `buildPollFds()` |
| Como trata pacote parcial? | buffer individual e extração somente no `\n` | `Client::appendToReadBuffer()` e `extractCommand()` |
| Como trata `send()` parcial? | conserva o restante na output queue | `handleWritable()` e `Client::consumeOutput()` |
| Como trata cliente lento? | fila limitada a 65536 e `POLLOUT` sob demanda | `Limits.hpp`, `Client::queueOutput()`, `buildPollFds()` |
| Como limpa desconexão? | marca, envia a fila somente após `POLLOUT` e fecha após esvaziar ou expirar o linger | `disconnectClient()`, `hasLingeringClients()`, `reapDisconnected()`, `sweepChannels()` |
| Quem vira operador? | primeiro membro que cria o canal | `joinOneChannel()` |
| Como mensagens chegam ao canal? | broadcast para todos exceto remetente | `cmdPrivmsg()` e `broadcastToChannel()` |
| Onde ficam os comandos? | tabela de handlers | `src/CommandTable.cpp` |
| Onde ficam modos e papéis? | objeto `Channel` | `include/Channel.hpp`, `src/Channel.cpp` |
