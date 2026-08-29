# FT_IRC — Régua de Avaliação

> Avaliação do projeto `ft_irc` da 42. A equipe avaliada é composta por 3 estudantes.

## Introdução

Por favor, siga as seguintes regras:

- Mantenha-se educado, cortês, respeitoso e construtivo durante todo o processo de avaliação. O bem-estar da comunidade depende disso.
- Identifique as possíveis disfunções no projeto do(a) aluno(a) ou grupo cujo trabalho está sendo avaliado. Dedique tempo para discutir e debater os problemas que possam ter sido identificados.
- Você deve considerar que pode haver algumas diferenças em como seus colegas podem ter entendido as instruções do projeto e o escopo de suas funcionalidades. Mantenha sempre a mente aberta e classifique-os da forma mais honesta possível. A pedagogia é útil apenas se a avaliação por pares for feita com seriedade.

## Guidelines

- Avalie apenas o trabalho enviado no repositório Git do(a) aluno(a) ou grupo avaliado.
- Verifique cuidadosamente se o repositório Git pertence ao(à) aluno(a) ou alunos(as). Garanta que o projeto é o esperado. Além disso, verifique se `git clone` é usado em um diretório vazio.
- Verifique cuidadosamente se nenhum alias malicioso foi usado para enganá-lo(a) e fazê-lo(a) avaliar algo que não é o conteúdo do repositório oficial.
- Para evitar surpresas e, se aplicável, revise em conjunto quaisquer scripts usados para facilitar a avaliação, como scripts de teste ou automação.
- Se você não concluiu o projeto que vai avaliar, você deve ler todo o subject antes de iniciar o processo de avaliação.
- Use as flags disponíveis para relatar um repositório vazio, um programa que não funciona, um erro de Norma, trapaça etc.
- Nesses casos, o processo de avaliação termina e a nota final é 0, ou -42 no caso de trapaça. No entanto, exceto em casos de trapaça, os(as) alunos(as) são fortemente encorajados(as) a revisar o trabalho enviado juntos para identificar quaisquer erros que não devem ser repetidos no futuro.
- Durante a defesa, nenhum segfault ou outra terminação inesperada, prematura ou descontrolada do programa é permitida; caso contrário, a nota final é 0. Use o sinalizador apropriado.
- Você nunca deve editar nenhum arquivo, exceto o arquivo de configuração, se ele existir. Se quiser editar um arquivo, reserve um tempo para explicitar os motivos com o(a) aluno(a) avaliado(a) e certifique-se de que ambos estão de acordo com isso.
- Verifique se não há vazamentos de memória. Qualquer memória alocada no heap deve ser devidamente liberada antes do término da execução do programa. Você pode usar ferramentas disponíveis no computador, como `leaks`, `valgrind` ou `e_fence`. Em caso de vazamentos de memória, marque a flag apropriada.

## Anexos

- [subject.pdf](https://cdn.intra.42.fr/pdf/pdf/214255/pt_br.subject.pdf)
- [bircd.tar.gz](https://cdn.intra.42.fr/document/document/52662/bircd.tar.gz)

# Parte Obrigatória

## README e Verificação de Conformidade

O repositório contém um arquivo `README.md` em sua raiz, e o arquivo inclui todo o seguinte:

- A primeira linha está em itálico e formatada exatamente como: *Este projeto foi criado como parte do currículo da 42 por <login1>[, <login2>[, <login3>[...]]].*
- Uma seção **Descrição** explicando o propósito do projeto e fornecendo uma breve visão geral.
- Uma seção **Instruções** com detalhes relevantes sobre compilação, instalação e/ou execução.
- Uma seção **Recursos** listando referências — documentação, tutoriais etc. — e explicando como a IA foi usada, especificando para quais tarefas e quais partes do projeto.

Se qualquer um dos elementos requeridos acima estiver faltando, a nota é 0.

## Verificações básicas

- Existe um `Makefile`.
- O projeto compila corretamente com as opções necessárias.
- O projeto é escrito em C++.
- O executável é nomeado conforme o esperado.
- Pergunte e verifique quantas chamadas `poll()` — ou equivalente — estão presentes no código. Deve haver apenas uma.
- Verifique se nenhuma chamada para `accept`, `read`/`recv` ou `write`/`send` acontece antes de uma chamada para `poll()` — ou equivalente.
- Após essas chamadas, `errno` não deve ser usado para acionar ações específicas, por exemplo ler novamente após `errno == EAGAIN`.
- Verifique se cada chamada para `fcntl()` é feita da seguinte forma:

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

- Qualquer outro uso de `fcntl()` é proibido.

Se algum desses pontos estiver errado, a avaliação termina imediatamente e a nota final é 0.

## Networking

Verifique os seguintes requisitos:

- O servidor inicia e escuta em todas as interfaces de rede na porta fornecida na linha de comando.
- Usando a ferramenta `nc`, você pode se conectar ao servidor, enviar comandos e o servidor responde.
- Pergunte à equipe qual é o cliente IRC de referência deles.
- Usando este cliente IRC, você pode se conectar ao servidor.
- O servidor pode lidar com várias conexões simultaneamente.
- O servidor não deve bloquear e deve ser capaz de responder a todas as demandas.
- Realize alguns testes com o cliente IRC e `nc` simultaneamente.
- Entre em um canal usando o comando apropriado. Certifique-se de que todas as mensagens de um cliente nesse canal sejam enviadas para todos os outros clientes que ingressaram no canal.

## Situações especiais de rede

As comunicações de rede podem ser perturbadas por muitas situações incomuns.

- Como no subject, usando `nc`, tente enviar comandos parciais. Verifique se o servidor responde corretamente.
- Com um comando parcial enviado, certifique-se de que outras conexões continuem a funcionar corretamente.
- Encerre inesperadamente um cliente. Em seguida, verifique se o servidor permanece operacional para outras conexões e para qualquer novo cliente que chegue.
- Encerre inesperadamente uma sessão `nc` com apenas metade de um comando enviado. Verifique novamente se o servidor não está em um estado incomum ou bloqueado.
- Suspenda um cliente (`Ctrl+Z`) conectado a um canal. Em seguida, inunde o canal com outro cliente. O servidor não deve congelar.
- Quando o cliente estiver ativo novamente, todos os comandos armazenados devem ser processados normalmente.
- Verifique também se há vazamentos de memória durante esta operação.

## Comandos básicos do cliente

- Com `nc` e com o cliente IRC de referência, verifique se você pode autenticar, definir um apelido, definir um nome de usuário e entrar em um canal. Isso deve ser o suficiente — você já deve ter feito isso antes.
- Verifique se as mensagens privadas (`PRIVMSG`) estão totalmente funcionais com vários parâmetros.

## Comandos de cliente operador de canal

- Com `nc` e com o cliente IRC de referência, verifique se um usuário normal não tem os privilégios para executar ações de operador de canal.
- Em seguida, teste com um operador.
- Todos os comandos de operação de canal devem ser testados.
- Deduza um ponto para cada recurso que não estiver funcionando.
