# Guia `ft_irc`

Este roteiro segue a mesma ordem e os mesmos subtítulos de
`docs/ft_irc_evaluation.md`. Ele foi escrito para esta implementação, cujo
executável é `ircserv`, cuja senha de exemplo é `secret` e cujo cliente IRC de
referência é o **irssi**.

## Convenções e preparação dos terminais

Use a porta `6667` e a senha `secret` em todo o roteiro. Se a porta estiver
ocupada, escolha outra porta livre entre `1024` e `65535` e substitua `6667`
em todos os comandos.

sugestao:

| Terminal | Uso principal |
|---|---|
| **T1** | compilação, servidor e depois Valgrind |
| **T2** | irssi da operadora `alice` |
| **T3** | irssi do usuário normal `bob` |
| **T4** | `nc`,  `carol`, e pacotes parciais |


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


> Este projeto implementa um servidor IRC em C++98. Ele aceita vários clientes
> TCP/IP sem `fork`, usa descritores não bloqueantes e um único `poll()` para
> multiplexar o socket de escuta e os clientes. O cliente de referência é o
> irssi. O servidor implementa registro com `PASS`, `NICK` e `USER`, canais,
> mensagens privadas e os comandos de operador `KICK`, `INVITE`, `TOPIC` e
> `MODE` com os modos `i`, `t`, `k`, `o` e `l`.




### 3. Quantas chamadas `poll()` existem?


Os comentários também contêm a palavra `poll`, mas há **uma única chamada
executável**, em `Server::run()`:

```cpp
int timeout = hasLingeringClients() ? irc::LINGER_POLL_MS : -1;
int ready = poll(&_pollFds[0], _pollFds.size(), timeout);
```

> Existe exatamente uma chamada a `poll()`, em `Server::run()`. Antes dela,
> `buildPollFds()` cria um vetor contendo o socket de escuta e todos os clientes.
> O mesmo retorno de `poll()` governa `accept`, leitura e escrita. O timeout é
> `-1` durante operação normal e passa a `LINGER_POLL_MS` somente enquanto um
> cliente marcado para desconexão aguarda a fila de saída esvaziar.


Socket de escuta e os clientes ativos têm `POLLIN`(socket pronto para leitura); `POLLOUT` (socket pronto para enviar dados) só
é armado quando aquele cliente possui saída pendente. Um cliente já marcado
para desconexão deixa de receber `POLLIN` e permanece apenas com `POLLOUT`
enquanto houver algo a enviar. Isso evita novos comandos depois do `QUIT` e
evita busy loop.

### 4. `accept`, `recv` e `send` em relação ao `poll()`

> O `accept()` só é chamado quando o único `poll()` marcou `POLLIN` no socket
> de escuta. O `recv()` só é chamado por `handleReadable()` após `POLLIN`. O
> único `send()` do projeto ocorre em `handleWritable()` após `POLLOUT`.


### 5. Uso de `errno` após as syscalls


> `accept()`, `recv()` e `send()` não consultam `errno`.

não pode usar `errno` para dirigir comportamento depois de `accept`, `recv` ou
`send`.

### 6. Uso permitido de `fcntl()`

Há duas chamadas executáveis:

```cpp
fcntl(_listenFd, F_SETFL, O_NONBLOCK);
fcntl(fd, F_SETFL, O_NONBLOCK);
```

Resposta sugerida:

> Uma chamada torna o socket de escuta não bloqueante; a outra torna cada
> socket retornado por `accept()` não bloqueante. 

## Networking

### 1. Iniciar o servidor

No **T1**:

```sh
./ircserv 6667 secret
```
### 2. Confirmar escuta em todas as interfaces

No **T5**, com o servidor ativo:

```sh
ss -ltnp 'sport = :6667'
```

 `0.0.0.0:6667` mostra que aceita conexoes de qualquer interface de rede.

### 3. Conectar e obter resposta com `nc`

- nc (netcat): ferramenta genérica para abrir conexões TCP. Ele não entende IRC: tudo que você digita é enviado diretamente ao servidor

- irssi é um cliente IRC completo de terminal. Ele entende canais, usuários, comandos, códigos numéricos e formata as mensagens para você


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

### 4. Cliente IRC de referência e conexão com irssi

**T2**:

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

irssi, digite como mensagem normal na janela de `#avaliacao`:

```text
mensagem da alice para todos
```

No T4:

```text
PRIVMSG #avaliacao :resposta do nc com espacos
```

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
`nc`. O servidor deve reconstruir **um único** comando `command`


### 2. Um parcial não pode bloquear outras conexões

Abra um novo `nc` no **T4** e digite `PING :par`, seguido de `Ctrl+D`, sem
Enter. O servidor ainda não deve responder porque a linha está incompleta.

Enquanto isso:

- no T2, Alice envia `servidor continua responsivo` em `#avaliacao`;
- Bob deve receber imediatamente no T3;

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

ctrl-c

### 4. Encerrar `nc` com metade de um comando

No T4, conecte um novo `nc`, registre `carol`, entre no canal e então digite:

```text
PRIVMSG #avaliacao :esta mensagem ficou pela metade
```

Não pressione Enter. Pressione `Ctrl+D` uma vez para enviar o fragmento e logo
depois `Ctrl+C` para matar o `nc`. A linha parcial não deve ser executada. No
T2 e T3, envie novas mensagens e confirme que o servidor continua normal.

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

### 6. Vazamentos de memória durante a operação

Encerre tudo
```sh
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
    ./ircserv 6667 secret
```

Reconecte Alice e Bob nos T2/T3, entre em `#avaliacao`, repita a suspensão,
flood controlado.

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
