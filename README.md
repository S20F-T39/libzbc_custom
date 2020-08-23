# libzbc_custom

libzbc를 커스텀하여 파일 입출력을 수행합니다.

> `write` 시에 `zone` 을 지정하는 방법
1. `Conventional Zone` 은 일단 건드리지 않는다. `Conventional zone`을 무시한 채로 진행.
2. `Empty Zone` 의 첫번째를 구하여 `Write`
3. `Empty Zone` 이 없고 `Implicit Open` 이 있을 때에는 
그 `Implicit Open Zone` 의 `Write Pointer` 를 구하여
해당 `Write Pointer` 부터 작성합니다.

> `zbc_list_zones` 함수
1. `empty_zones` 변수에 상태가 `Empty` 인 zone 담음 (배열이라고 생각하면 간단?)
2. nr_zone 변수에 zone 개수를 지정해 줍니다.

> `buf_size` 변수
1. `buf_size` 변수는 I/O 시의 Block Size 를 지칭하는 것으로 판단되었습니다.
2. `buf_size` 변수를 이미 시작할 때 512B 로 가정하고 진행하였습니다.

> `write` 단계
1. `write` 단계에서, `vector` 단위 I/O 는 고려하지 않았습니다.
2. `vector` 단위의 I/O 를 고려하지 않았기 때문에, `io_size` 와 `buf_size` 가 512B 로 같습니다.

> `file write` 단계
1. 파일 입력이 `argument` 에서 강제되고 있기 때문에, `file I/O` 가 필수적입니다.
2. `file size` 는 기본적으로 `Byte` 단위로 표현됩니다.
3. `file size` 가 `zone` 의 크기보다 크다면, 일단은 `zone` 을 넘어가 `write` 하도록 하였습니다.