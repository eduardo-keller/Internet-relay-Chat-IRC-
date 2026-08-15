# Plano passo a passo — Fase 1 DOMAIN

Este documento guia a implementação do modelo `Channel`, responsabilidade da
trilha DOMAIN (Bruno). O contrato técnico continua sendo
[`ARCHITECTURE.md`](ARCHITECTURE.md), e o estado oficial continua sendo
[`TASKS.md`](TASKS.md). Se houver divergência, o contrato e os requisitos do
projeto vencem este roteiro.

## 1. Objetivo e limite da fase

Ao final desta fase devem existir:

- `src/Channel.cpp`, implementando toda a interface de `include/Channel.hpp`;
- `tests/test_channel.cpp`, cobrindo estado, coleções, modos e OCF;
- `runChannelTests()` registrado no harness;
- todos os testes passando com C++98 e as flags obrigatórias;
- os itens de `Channel` da Fase 1 marcados como `done` em `TASKS.md`.

Não faz parte desta fase:

- implementar `JOIN`, `PART`, `PRIVMSG`, `TOPIC`, `KICK`, `INVITE` ou `MODE`;
- implementar métodos de `Server`;
- usar sockets, file descriptors, `poll`, `send` ou `recv`;
- alterar a interface pública de `Channel.hpp` sem combinar com Eduardo.

`Channel` é um objeto de domínio puro. Ele guarda ponteiros não proprietários
para clientes e nunca cria ou destrói um `Client`.

## 2. Ciclo obrigatório em cada passo

Use o mesmo ciclo em todos os passos abaixo:

1. Escreva o menor teste que demonstra o próximo comportamento.
2. Rode `make test` e confirme que o teste novo falha pelo motivo esperado.
3. Implemente somente o necessário para fazê-lo passar.
4. Rode `make test` novamente.
5. Leia o diff e confirme que não entrou C++11 nem código de rede.
6. Atualize o item correspondente em `TASKS.md` no mesmo commit do código.

Nunca faça commit no estado vermelho. O teste falhando é uma verificação local
do ciclo TDD; o commit deve terminar sempre verde.

Antes do primeiro passo, registre a linha de base:

```sh
git switch domain
git status --short
make test
```

Resultado observado antes desta implementação: `173 passed, 0 failed`. Se a
linha de base não estiver verde, não comece o `Channel`: identifique primeiro
se houve uma mudança nova na `main`.

## 3. Passo 0 — Conectar o novo arquivo ao harness

### Teste/alteração

Crie `tests/test_channel.cpp` contendo apenas a função pública:

```cpp
void runChannelTests(void)
{
}
```

Depois:

- declare `void runChannelTests(void);` em `tests/harness.hpp`;
- chame `runChannelTests();` em `tests/test_main.cpp`;
- rode `make test`.

O `Makefile` já usa `tests/*.cpp`, então não deve ser alterado. Este passo
confirma que o arquivo foi descoberto, compilado e linkado sem misturar ainda
um comportamento real.

### Pronto quando

- `make test` continua mostrando 173 testes passando;
- o binário de testes chama `runChannelTests()`;
- não existe um segundo `main()`.

## 4. Passo 1 — Estado inicial, nome e tópico

Comece `src/Channel.cpp` com os construtores, destrutor e os métodos necessários
para esta fatia. Todos os campos escalares devem ser inicializados
explicitamente; não dependa de valores indefinidos.

### Testes a adicionar

Crie uma função estática, por exemplo `testIdentityAndTopic()`, e verifique:

- o construtor padrão cria nome e tópico vazios;
- `Channel("#chat")` preserva exatamente o nome recebido;
- o tópico começa vazio;
- `setTopic("General discussion")` preserva espaços;
- um tópico pode ser substituído;
- `setTopic("")` limpa o tópico.

Também verifique o estado inicial observável:

- nenhum membro e `isEmpty()` verdadeiro;
- modos `i` e `t` desligados;
- `hasKey()` falso e chave vazia;
- `hasUserLimit()` falso e limite igual a zero.

### Implementação mínima

Implemente:

- construtor padrão;
- construtor com nome;
- destrutor;
- `getName`, `getTopic` e `setTopic`;
- getters mínimos necessários para provar o estado inicial.

Os dois construtores devem inicializar os booleanos como `false` e o limite
como `0` usando lista de inicialização.

### Verificação

```sh
make test
```

Marque `Channel — OCF, nome, tópico` como `doing`; ele só vira `done` depois do
teste completo de cópia e atribuição no Passo 8.

## 5. Passo 2 — Membros

### Testes a adicionar

Use dois ou três objetos `Client` locais e passe seus endereços ao canal.
Verifique:

- canal novo está vazio e tem contagem zero;
- `addMember(&alice)` torna Alice membro;
- a contagem muda para um;
- adicionar Alice novamente não cria duplicata;
- adicionar Bob mantém ambos e muda a contagem para dois;
- `getMembers()` contém os mesmos endereços adicionados;
- remover Alice não remove Bob;
- remover quem não pertence ao canal é uma operação segura;
- remover o último membro torna o canal vazio;
- consultar, adicionar ou remover `NULL` não cria um membro inválido.

### Implementação mínima

Implemente `addMember`, `removeMember`, `isMember`, `isEmpty`, `memberCount` e
`getMembers` usando `std::set<Client *>`.

Não use nickname como identidade e não acesse `Client::getFd()`. O endereço do
objeto é a identidade dentro do canal. Ignore `NULL` em inserções.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — membros` como `done`.

## 6. Passo 3 — Operadores

### Testes a adicionar

Verifique:

- um cliente começa sem privilégio de operador;
- depois de `addOperator`, `isOperator` retorna verdadeiro;
- adicionar o mesmo operador duas vezes não duplica estado;
- dois operadores podem coexistir;
- `removeOperator` remove somente o alvo;
- remover um operador inexistente é seguro;
- `NULL` nunca é considerado operador.

Adicione o cliente como membro antes de promovê-lo no teste. O handler de
`JOIN` será responsável, na Fase 3, por fazer o primeiro membro virar operador.
Não implemente essa política dentro do construtor de `Channel`.

### Implementação mínima

Implemente `addOperator`, `removeOperator` e `isOperator` com o conjunto
`_operators`, ignorando `NULL` em inserções.

Não atribua aqui comportamentos que o contrato não declarou. Em especial,
`removeMember` e `removeOperator` continuam sendo operações explícitas; quem
processar `PART`, `KICK` ou desconexão deverá executar a limpeza necessária.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — operadores` como `done`.

## 7. Passo 4 — Lista de convites

### Testes a adicionar

Verifique:

- cliente não convidado retorna falso;
- `addInvite(&bob)` registra o convite;
- adicionar o mesmo convite novamente não duplica estado;
- mudar `bob.setNickname(...)` não perde o convite;
- `removeInvite(&bob)` consome o convite;
- remover convite inexistente é seguro;
- `NULL` nunca é considerado convidado.

O teste de mudança de nickname é obrigatório: ele prova por comportamento que
o canal guarda `Client*`, e não uma cópia do nickname.

### Implementação mínima

Implemente `addInvite`, `removeInvite` e `isInvited` com `_invited`.

Nesta fase, o consumo é testado chamando `removeInvite` explicitamente. O
`cmdJoin`, na Fase 3, fará essa chamada somente depois de um JOIN convidado ter
sido aceito.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — lista de convites` como `done`.

## 8. Passo 5 — Modos booleanos `i` e `t`

### Testes a adicionar

Verifique separadamente para cada modo:

- começa desligado;
- o setter com `true` liga;
- repetir `true` mantém ligado;
- o setter com `false` desliga;
- alterar `i` não altera `t`, e vice-versa.

### Implementação mínima

Implemente:

- `isInviteOnly` e `setInviteOnly`;
- `isTopicRestricted` e `setTopicRestricted`.

Esses métodos apenas guardam estado. As decisões “pode entrar?” e “pode mudar
o tópico?” pertencem aos handlers futuros.

### Verificação e tasks

```sh
make test
```

Com os testes verdes, marque os itens dos modos `i` e `t` como `done`.

## 9. Passo 6 — Modo `k` e ciclo de vida da chave

### Testes a adicionar

Verifique:

- canal começa sem chave;
- `setKey("secret")` liga `hasKey()` e preserva a chave;
- uma segunda chamada substitui a chave anterior;
- `clearKey()` desliga `hasKey()` e apaga a string armazenada;
- chamar `clearKey()` novamente é seguro.

Não valide a sintaxe da chave dentro de `Channel`. Na Fase 3, `cmdMode`
validará presença de parâmetro antes de chamar `setKey`.

### Implementação mínima

Implemente `hasKey`, `getKey`, `setKey` e `clearKey`. `clearKey` deve atualizar
o booleano e limpar `_key`, evitando estado antigo observável.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — modo k (key)` como `done`.

## 10. Passo 7 — Modo `l` e ciclo de vida do limite

### Testes a adicionar

Verifique:

- canal começa sem limite e com valor zero;
- `setUserLimit(10)` liga `hasUserLimit()` e armazena 10;
- uma segunda chamada substitui o limite anterior;
- `clearUserLimit()` desliga `hasUserLimit()` e restaura o valor para zero;
- chamar `clearUserLimit()` novamente é seguro.

Não faça parsing de string em `Channel`. Na Fase 3, `cmdMode` usará
`utils::parseInt` e aceitará somente um limite válido antes de chamar o modelo.

### Implementação mínima

Implemente `hasUserLimit`, `getUserLimit`, `setUserLimit` e
`clearUserLimit`.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — modo l (limit)` como `done`.

## 11. Passo 8 — OCF completo

Agora que todo o estado existe, prove a Forma Canônica Ortodoxa por
comportamento. A cópia dos conjuntos é uma cópia dos containers; os ponteiros
continuam apontando para os mesmos `Client` existentes e continuam sendo não
proprietários.

### Testes a adicionar

Monte um canal original com:

- nome e tópico;
- dois membros;
- pelo menos um operador;
- um convite;
- `i` e `t` ligados;
- chave e limite ativos.

Teste então:

- construtor de cópia preserva todo o estado observável;
- atribuição preserva todo o estado observável;
- modificar a cópia não modifica os conjuntos nem os escalares do original;
- autoatribuição mantém o objeto íntegro, usando um alias como já ocorre nos
  testes de `Client` para evitar warning de autoatribuição sob `-Werror`;
- destruir uma cópia não destrói os objetos `Client` apontados.

### Implementação mínima

Implemente ou complete o construtor de cópia e `operator=` copiando todos os
campos. Proteja a atribuição com `if (this != &other)`.

O destrutor não executa `delete` em membro, operador ou convidado. Os conjuntos
são destruídos automaticamente.

### Verificação e task

```sh
make test
```

Com os testes verdes, marque `Channel — OCF, nome, tópico` como `done`.

## 12. Passo 9 — `modeString`

A saída deve ser determinística porque será usada no numeric 324. A ordem
definida para esta implementação é `i`, `t`, `k`, `l`, seguida dos parâmetros
na mesma ordem dos modos que os consomem.

### Decisão adotada para o contrato atual

Adote a interpretação literal do parâmetro `includeParams`:

- `modeString(true)` inclui chave e limite quando ativos;
- `modeString(false)` apresenta somente as flags, sem chave nem limite;
- quando nenhum modo está ativo, o resultado é `"+"`.

Assim, um canal com todos os modos, chave `secret` e limite 10 produz:

```text
modeString(true)  -> "+itkl secret 10"
modeString(false) -> "+itkl"
```

Esta é a decisão recomendada porque corresponde ao nome `includeParams`, não
exige alteração em `Channel.hpp` e garante que a chave não seja revelada. No
futuro, `cmdMode` poderá chamar
`channel.modeString(channel.isMember(&sender))`: membros recebem flags e
parâmetros; não membros recebem somente as flags.

Essa decisão não bloqueia nem altera uma tarefa TRANSPORT. O `Server` apenas
entrega a linha montada pelo handler DOMAIN; ele não interpreta o retorno de
`modeString`. Portanto, Bruno pode concluir toda a Fase 1 DOMAIN sem esperar
uma implementação do Eduardo. Basta comunicar a decisão na revisão do PR.

Se depois a dupla preferir mostrar o limite a não membros e esconder somente a
chave, isso será uma mudança de contrato: o nome `includeParams` deixará de
descrever o comportamento. Nesse caso, atualizem juntos `Channel.hpp`, os
testes e `ARCHITECTURE.md` antes de mudar a implementação.

### Testes a adicionar

Teste pelo menos esta matriz:

| Estado | Chamada | Resultado esperado |
|---|---|---|
| nenhum modo | `modeString(true)` | `+` |
| somente `i` | `modeString(true)` | `+i` |
| `i` e `t` | `modeString(true)` | `+it` |
| somente chave `secret` | `modeString(true)` | `+k secret` |
| somente limite 10 | `modeString(true)` | `+l 10` |
| todos ativos | `modeString(true)` | `+itkl secret 10` |
| todos ativos | `modeString(false)` | `+itkl` |
| chave limpa | `modeString(true)` | não contém `k` nem `secret` |
| limite limpo | `modeString(true)` | não contém `l` nem o limite antigo |

Também altere os modos em ordem diferente e confirme que a string final
continua em `itkl`. A saída não pode depender da ordem das chamadas aos
setters.

### Implementação mínima

Monte primeiro a parte das flags. Depois acrescente os parâmetros. O contrato
compartilhado oferece `utils::toString(int)`, mas `_userLimit` é
`std::size_t`: não deixe essa conversão acontecer implicitamente. No fluxo do
servidor, `cmdMode` primeiro usa `utils::parseInt`, rejeita zero e negativos e
só então chama `setUserLimit`; portanto, o limite aceito está no intervalo
positivo de `int`. Use `utils::toString(static_cast<int>(_userLimit))` e inclua
um teste com `INT_MAX`. Não altere a assinatura compartilhada de
`utils::toString` sem combinar com Eduardo. Também não use `std::to_string`,
que não existe em C++98.

### Verificação e task

```sh
make test
```

Com toda a matriz verde, marque `Channel::modeString` como `done`.

## 13. Passo 10 — Regressão e fechamento da fase

Rode uma reconstrução limpa para impedir que objetos antigos escondam um erro:

```sh
make fclean
make
make test
make
```

Confirme:

- compilação com `-Wall -Wextra -Werror -std=c++98` sem warnings;
- todos os 173 testes anteriores continuam passando;
- todos os testes novos de `Channel` passam;
- o último `make` informa que não há trabalho a fazer, sem relink;
- `git diff --check` não encontra whitespace inválido;
- `git status --short` mostra somente os arquivos esperados;
- nenhum arquivo DOMAIN inclui `<poll.h>` ou chama funções de socket;
- `TASKS.md` mostra todos os itens `Channel` da Fase 1 como `done`;
- o item “os dois leram o ARCHITECTURE e aceitaram o seam” só é atualizado
  depois de Bruno realmente confirmar o contrato com Eduardo.

Comandos auxiliares:

```sh
git diff --check
git diff -- include/Channel.hpp src/Channel.cpp tests/test_channel.cpp \
  tests/harness.hpp tests/test_main.cpp docs/TASKS.md
```

O critério de pronto não é apenas “compila”: cada método público de `Channel`
deve ter ao menos um teste positivo e um caso de remoção, repetição ou estado
inicial aplicável.

## 14. Sugestão de commits pequenos

Uma sequência possível, sempre verde:

1. `add channel test entry point`
2. `implement channel identity and members`
3. `implement channel operators and invites`
4. `implement channel mode state`
5. `complete channel copy semantics`
6. `render channel mode string`

Se vários passos forem feitos na mesma sessão, ainda assim rode `make test`
entre eles. Não espere o final para descobrir qual mudança causou a regressão.

## 15. Próximo checkpoint depois desta fase

Depois que este roteiro estiver completo, não avance diretamente para os
handlers. O próximo checkpoint do plano principal é a sessão conjunta para o
laço de `poll()`. Enquanto Eduardo implementa o restante da Fase 2, Bruno pode
revisar e explicar a trilha TRANSPORT. Os handlers DOMAIN começam quando o seam
público do `Server` tiver implementação utilizável e testável.
