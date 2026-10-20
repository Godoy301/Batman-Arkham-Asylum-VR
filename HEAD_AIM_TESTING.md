# Primeira pessoa: mira e HUD pelo headset

Build de desenvolvimento: `Asylum-0.1.4-HEAD-AIM`, baseado no fork
`Godoy301/Batman-Arkham-Asylum-VR`, versão 0.1.4.

Ative **FIRST PERSON** e **aim and HUD follow the headset** no painel VR.
O segundo ajuste fica ativado por padrão quando não existe configuração salva.
O analógico direito continua girando a direção base; o olhar é somado a ela.
O HUD acompanha posição e orientação do headset, incluindo inclinação, em um
único plano com profundidade para os dois olhos. Marcadores de alvos continuam
presos aos alvos no mundo.

O ajuste é salvo em `%LOCALAPPDATA%\Batman_Asylum\aa_rendering.ini`, seção
`AsylumVR`, chave `FirstPersonLookAim`. Comandos: `aa fp aim on`, `aa fp aim off`,
`aa fp status`. Desativar a opção restaura a orientação base e o HUD na sala.

## Compilação e alcance da validação

No Windows, use os comandos de compilação do README. O novo CTest se chama
`first_person_head_aim`. O workflow **Windows head aim checks** compila o mod
Win32 e executa quatro testes de CPU; ele não executa o jogo, testes de GPU,
OpenXR, nem compila o helper DLSS de 64 bits.

O artefato `head-aim-test-dlls` contém somente as DLLs de teste. Ele requer uma
instalação funcional do mod 0.1.4, com seu helper e dependências. Com o jogo
fechado, faça uma cópia das três DLLs existentes em `Binaries` antes de
substituí-las pelas DLLs do artefato. Para voltar, restaure essa cópia. Este
artefato não é o instalador completo de uma nova versão.

Os testes portáveis verificam a direção de mira contra raios independentes,
giro do controle somado ao headset, ausência de acumulação da rotação do
headset, recentralização, restauração, poses inválidas e projeção binocular
do HUD sob movimento. Eles não provam o comportamento dos gadgets no jogo.

## Uma sessão de teste no PC VR

1. Inicie pelo atalho VR. No menu principal, confirme o comportamento normal.
2. Entre no jogo e ative primeira pessoa. Sem tocar no analógico direito,
   olhe para esquerda/direita, cima/baixo e incline a cabeça. O HUD deve
   permanecer à frente, com a mesma profundidade nos dois olhos.
3. Mire com um gadget em dois alvos separados. Mova apenas a cabeça, depois
   use o analógico com a cabeça parada e, por fim, mova os dois juntos.
   Compare a mira visível com a direção/impacto real do gadget. Teste também
   a seleção de pontos do gancho: seu marcador deve continuar no alvo.
4. Olhe para um lado por alguns segundos sem mover o controle: não deve
   haver giro contínuo nem aplicação duplicada da rotação. Gire através da
   direção oposta, recentralize e repita com o outro lado.
5. Ligue/desligue a opção, abra/feche o painel VR, teste pausa/mapa, uma
   transição de câmera e retorno ao menu principal. Teste perda/retorno do
   rastreamento. Verifique controles, retorno da mira e ausência de saltos
   inesperados. Repita com DLSS e, se utilizado, AFW.
6. Diante de qualquer problema, pressione **F2**, saia normalmente e preserve
   os arquivos abaixo antes de iniciar várias novas sessões.

## Evidências necessárias

Em `%LOCALAPPDATA%\Batman_Asylum`:

- `batman_asylum.log`: sessão atual; `batman_asylum.prev.log`: sessão anterior.
  O logger já preserva uma geração anterior e descarrega cada mensagem em disco.
- `captures\A2p_*\status.txt`: inclui o identificador do build e `headAim=`.
  A mesma pasta contém as imagens disponíveis de cena, HUD e olhos.
- `captures\A2p_*\head_aim.csv`: até 256 amostras recentes, no máximo 20 por
  segundo durante atividade, mais mudanças de estado. Inclui tempo monotônico
  em milissegundos, geração, orientação solicitada/observada, quaternion e
  analógico. O histórico fica em memória e só é escrito ao capturar, permitindo
  examinar movimentos entre as mensagens de cinco segundos do log.
- `captures\A2p_history_*`: histórico estéreo solicitado por F2, quando existem
  pares completos disponíveis. Os nomes A2p são herdados do capturador original.

As linhas `[aa head aim]` são emitidas nas mudanças de estado e a cada cinco
segundos, evitando escrita em disco por frame. Elas registram:

- `requested/enabled/active`, `reason`, `gen` e `ageMs`: configuração,
  aplicação efetiva, motivo de suspensão, geração da pose e idade da última escrita;
- `bodyPitchYaw`, `targetPitchYaw`, `beforePitchYaw`, `readbackPitchYaw`:
  orientação base, solicitada, observada antes da atualização e relida após ela;
- `headQuat` e `stickRXRY`: orientação do headset e último analógico entregue
  pelo compositor de entrada, amostrados na atualização;
- `writes/failures/restores`, `hudGen/hudFollowing/hudComposites/hudFailures`:
  escritas confirmadas, falhas, restaurações e composição do HUD;
- `fpFrames/headFrames/headMisses`: aplicação da câmera e leitura do osso da cabeça.

Uma releitura correta prova somente que `Controller.Rotation` recebeu o valor;
ela não prova que todos os gadgets o consumiram. A atualização ocorre na
construção da câmera, depois da simulação daquele frame: a reação do gadget
pode ocorrer no próximo tick. Compare imagens/impactos e os logs existentes
`[aa grapple]` para confirmar o resultado real. `ageMs` crescente ou gerações
paradas indicam ausência de novas atualizações, mesmo se a última amostra estava ativa.

## Limitações conhecidas

O repositório identifica o menu principal, mas ainda não possui um sinal
validado para todos os menus de pausa/mapa ou todas as câmeras de cinematics.
Esta mudança suspende a mira no menu principal/estado desconhecido, quando o
painel VR possui a entrada, sem pose nativa válida, sem pawn/controlador válido
ou com câmera distante do Batman. Pausa e cinematics próximas precisam do
teste acima; não há uma nova alegação de detecção desses estados.

Neste ambiente Linux, o build MSVC e a validação no headset não estão disponíveis.
O resultado do workflow Windows e a sessão real precisam ser consultados
separadamente; passar nos testes de CPU não significa aprovação no headset.
