*Este projeto foi criado como parte do currículo da 42 por brnascim, ekeller-.*

# ft_irc

## Descrição

`ft_irc` é um servidor IRC desenvolvido em C++98 para a parte obrigatória do
projeto da 42. Seu objetivo é permitir que vários clientes se conectem por
TCP/IP, façam autenticação e conversem por mensagens privadas ou por canais,
sem que uma conexão bloqueie as demais.

O servidor foi desenvolvido e validado usando o **irssi** como cliente de
referência. Ele implementa registro com `PASS`, `NICK` e `USER`, mensagens com
`PRIVMSG`, gerenciamento de canais com `JOIN` e `PART`, comandos de operador
`KICK`, `INVITE`, `TOPIC` e `MODE`, além dos modos de canal `i`, `t`, `k`, `o`
e `l`. Também trata `PING`, `PONG`, `QUIT`, `CAP`, `WHO` e `WHOIS` para manter
compatibilidade com o fluxo real do cliente.

### Visão geral da arquitetura

- `Server` possui os clientes e canais, mantém os sockets não bloqueantes e
  executa um único laço de `poll()` para leitura, escrita e novas conexões.
- `Client` armazena identidade, estado de registro e buffers de entrada e saída.
- `Channel` representa membros, operadores, convites, tópico e modos sem tocar
  em sockets ou file descriptors.
- `Message`, `Replies` e `Utils` concentram parsing, respostas numéricas,
  casemapping IRC e validações reutilizáveis.
- Os handlers recebem objetos e strings e acessam a rede somente pela interface
  pública de `Server`.

Entre as escolhas técnicas estão filas de saída para `send()` parcial,
desconexão adiada para evitar ponteiros pendurados, limites explícitos para
todos os buffers, nomes IRC insensíveis a maiúsculas e mensagens limitadas a
512 bytes incluindo `\r\n`.

## Instruções

### Pré-requisitos

- sistema Unix ou Linux;
- compilador C++ com suporte a C++98;
- GNU Make;
- opcionalmente `irssi` ou `nc` para testes manuais;
- opcionalmente Valgrind para os testes de memória.

O projeto utiliza somente a biblioteca padrão e as funções de sistema
permitidas pelo enunciado; não há dependências externas para instalar.

### Compilação

Na raiz do repositório:

```sh
make
```

Isso gera o executável `./ircserv` usando:

```text
c++ -Wall -Wextra -Werror -std=c++98
```

Outros alvos disponíveis:

```sh
make clean    # remove os arquivos objeto
make fclean   # remove objetos e executáveis
make re       # recompila tudo do zero
make test     # compila e executa os testes unitários
```

### Execução

```sh
./ircserv <porta> <senha>
```

A porta deve estar entre `1024` e `65535`, e a senha não pode ser vazia. Por
exemplo:

```sh
./ircserv 6667 secret
```

Em outro terminal, conecte-se com o irssi:

```sh
irssi
/connect localhost 6667 secret
/join #test
```

Para abrir dois clientes diretamente pela linha de comando:

```sh
irssi -c localhost -p 6667 -w secret -n alice
irssi -c localhost -p 6667 -w secret -n bob
```

Também é possível inspecionar o protocolo diretamente com netcat:

```sh
nc -C 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
JOIN #test
PRIVMSG #test :hello
```

### Testes

Os testes unitários exercitam parser, validações, buffers, registro, canais,
servidor e handlers sem depender de uma conexão externa:

```sh
make test
```

Os testes de integração em `tests/it/` iniciam o servidor em portas locais,
conversam com ele por TCP e validam o protocolo completo. Alguns deles usam
Valgrind:

```sh
for test in tests/it/*.sh; do
    "$test" || break
done
```

Cada script também pode ser executado isoladamente e aceita uma porta opcional:

```sh
./tests/it/join.sh
./tests/it/mode.sh 6800
./tests/it/full_session.sh
```

## Estrutura do repositório

```text
include/    interfaces e constantes compartilhadas
src/        servidor, modelo de domínio, parser e handlers
tests/      testes unitários
tests/it/   testes de integração por TCP
docs/       contrato de arquitetura, planos e requisitos do projeto
```

Documentos internos importantes:

- [Contrato de arquitetura](docs/ARCHITECTURE.md)
- [Requisitos do projeto](docs/ft_irc_requirements.md)
- [Plano geral](docs/PLANO.md)
- [Tarefas e estado do projeto](docs/TASKS.md)
- [Plano da Fase 2](docs/FASE2.md)
- [Plano da Fase 3](docs/FASE3.md)

## Recursos

### Referências

- [RFC 2812 — Internet Relay Chat: Client Protocol](https://www.rfc-editor.org/rfc/rfc2812.html): formato das mensagens, comandos e respostas numéricas do protocolo IRC.
- [RFC 1459 — Internet Relay Chat Protocol](https://www.rfc-editor.org/rfc/rfc1459.html): referência histórica usada para comparar comportamentos adotados por servidores e clientes reais.
- [POSIX `poll()`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html): especificação da multiplexação utilizada pelo servidor.
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/): introdução prática a sockets, TCP e programação de rede.
- [Documentação do irssi](https://irssi.org/documentation/): comandos e comportamento do cliente escolhido como referência.

### Uso de inteligência artificial

Ferramentas de IA foram usadas como apoio durante o desenvolvimento para:

- analisar o enunciado, os RFCs e os formatos das respostas IRC;
- estruturar os documentos de arquitetura, divisão de tarefas e planos das
  fases;
- sugerir e revisar implementações em `Channel`, `Client`, `Server`, parser,
  utilitários e handlers de comandos;
- elaborar casos de teste unitários e de integração, especialmente para
  pacotes fragmentados, limites de buffer, desconexão adiada, modos de canal e
  compatibilidade com o irssi;
- investigar erros de compilação, linking, protocolo e conformidade com C++98;
- revisar comentários e documentação.

As decisões finais e o código foram revisados pelos autores e validados com
compilação estrita, testes automatizados, sessões reais com irssi e execuções
sob Valgrind. A IA foi utilizada como ferramenta de apoio, não como substituta
da compreensão e da validação do projeto.
