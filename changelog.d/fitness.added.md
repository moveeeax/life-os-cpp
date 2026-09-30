Модуль `fitness`: перенос синхронизации Mi Fitness из mi-fitness-api 1.9.2
(клиент облака Xiaomi, синк, восемь типов данных здоровья, выгрузка schema
1.0/csv) под `/api/v1/fitness/*` с битами прав `kFitnessRead`/`kFitnessSync`
и ролями Fitness Reader/Operator; выключатель `FITNESS_ENABLED`.
