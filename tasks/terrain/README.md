# Террейн

В данном задании вам предстоит нарисовать террейн и познакомиться с тесселляционными шейдерами.

## Перед началом

 1. Скопируйте в эту папку любой семпл уже рисующий что-то.
    В идеале это должно быть решение [many_objects](/tasks/many_objects/) или [model_bakery](/tasks/model_bakery/), но можно скопировать и заготовку-рендерилку из задания [model_bakery](/tasks/model_bakery/).
    Не забудьте поменять название таргета в CMake.
 2. Вспомните материал занятий по тесселляциионными шейдерам.

## Задание

#### Шаг 1

На старте программы сгенерируйте при помощи многооктавного шума Перлина одноканальную R32_SFLOAT текстуру 4096х4096.
Это будет наша карта высот.
Альтернативно можно найти хорошую карту высот с лицензией CC0 в интернете и загрузите её, но это сильно менее интересно.

#### Шаг 2

Введём понятие *чанка* &mdash; квадратного куска террейна некоторого размера, который мы будем рисовать через один вызов `VkCmdDrawIndexed`.
Рендерить карту высот мы будем как сетку из чанков.
Напишите шейдер, рендерящий один чанк при помощи тесселляционных шейдеров:

- вершинный шейдер должен лишь генерировать "квадрат" из четырёх вершин аналогично [quad.vert](/common/render_utils/shaders/quad.vert);
- шейдер tessellation control должен разбивать "квадрат" на сетку мелкости обратно пропорциональной расстоянию до камеры;
- шейдер tessellation evaluation должен семплировать в каждой вершине карту высот и смещать вершину по вертикали;
- фрагментный шейедер должен правдоподобно рассчитывать диффузное освещение.

Подумайте, откуда взять нормали для вычисления диффузного освещения.
Отладив шейдер на примере одного чанка, сделайте цикл по сетке чанков, рисующий каждый чанк независимо и с разной тесселляцией.

#### Шаг 3

Придумайте, как побороть "дырки" в террейне на границах чанков.
Не забывайте поглядывать в [документацию по теселляции](https://docs.vulkan.org/spec/latest/chapters/tessellation.html), там есть крайне полезные картинки!

## Бонусный уровень

Конечно же, подобный наивный подход с чанками не масштабируется на столько крупный террейн как в играх вроде War Thunder 😉.
Чанки имеют одинаковый размер, а когда мы хотим видеть террейн аж до горизонта летая на самолёте, дистанция отрисовки должна превышать 100 километров.
Если мы возьмём чанк слишком большим, то приземлившись на землю мы получим слишком много треугольников из-за теселляции.
Если слишком маленьким, то получим слишком много дроу-коллов.
И в обоих случаях получим 3 FPS.
Более того, это какая ж текстура должна быть, чтобы покрывать область более чем 100x100 километров с точностью хотя бы в несколько метров, а желательно несколько сантиметров?

Секрет тут заключается в использовании *клипмапы*, процедурной генерации текстур террейна и *тороидальном обновлении*.
Попробуйте реализовать из этого столько, сколько сможете, следуя [статье из GPU Gems 2](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry).

Также в некоторых движках вместо тороидальных клипмап используют *разреженные виртуальные текстуры* (sparse virtual textures, SVT), а иногда даже смешивают эти 2 подхода.
SVG &mdash; более гибкий подход хранения крупных текстур на GPU, способный работать с более сложной геометрией чем "горизонтальный" террейн, например с пещерами, планетами или нестандартной гравитацией.
Альтернативно можете использовать этот подход, или любую их комбинацию на ваше усмотрение.
Пространство для дизайна тут огромное, главное понимать, чего вы хотите добиться, и внимательно смотреть в профайлер!

## Полезные материалы

 1. https://docs.vulkan.org/spec/latest/chapters/tessellation.html &mdash; документация по тесселляции
 2. https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry &mdash; про клипмапы и рендеринг террейна
 3. https://silverspaceship.com/src/svt/ &mdash; про разреженные виртуальные текстуры
