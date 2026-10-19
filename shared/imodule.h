#ifndef IMODULE_H
#define IMODULE_H

#include <string_view>

class IModule
{
public:
    virtual ~IModule() = default;

    // Уникальный идентификатор модуля
    virtual std::string_view getId() const = 0;

    // Человекочитаемое имя
    virtual std::string_view getName() const = 0;

    // Версия модуля (например, "1.0.0").
    virtual std::string_view getVersion() const = 0;

    // Главный метод, который запускает работу модуля.
    // Пока не принимает параметров, чтобы было проще на старте.
    virtual void execute() = 0;
};

#endif // IMODULE_H
