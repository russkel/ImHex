#include <hex/pattern_language/pattern_language.hpp>

#include <hex/helpers/file.hpp>
#include <hex/providers/provider.hpp>

#include <hex/pattern_language/preprocessor.hpp>
#include <hex/pattern_language/lexer.hpp>
#include <hex/pattern_language/parser.hpp>
#include <hex/pattern_language/validator.hpp>
#include <hex/pattern_language/evaluator.hpp>

#include <unistd.h>

namespace hex::pl {

    class PatternData;

    PatternLanguage::PatternLanguage() {
        this->m_preprocessor = new Preprocessor();
        this->m_lexer = new Lexer();
        this->m_parser = new Parser();
        this->m_validator = new Validator();
        this->m_evaluator = new Evaluator();

        this->m_preprocessor->addPragmaHandler("endian", [this](std::string value) {
            if (value == "big") {
                this->m_defaultEndian = std::endian::big;
                return true;
            } else if (value == "little") {
                this->m_defaultEndian = std::endian::little;
                return true;
            } else if (value == "native") {
                this->m_defaultEndian = std::endian::native;
                return true;
            } else
                return false;
        });

        this->m_preprocessor->addPragmaHandler("eval_depth", [this](std::string value) {
            auto limit = strtol(value.c_str(), nullptr, 0);

            if (limit <= 0)
                return false;

            this->m_recursionLimit = limit;
            return true;
        });

        this->m_preprocessor->addPragmaHandler("base_address", [](std::string value) {
            auto baseAddress = strtoull(value.c_str(), nullptr, 0);

            SharedData::currentProvider->setBaseAddress(baseAddress);
            return true;
        });

        this->m_preprocessor->addDefaultPragmaHandlers();
    }

    PatternLanguage::~PatternLanguage() {
        delete this->m_preprocessor;
        delete this->m_lexer;
        delete this->m_parser;
        delete this->m_validator;
        delete this->m_evaluator;
    }


    std::optional<std::vector<PatternData*>> PatternLanguage::executeString(prv::Provider *provider, const std::string &string) {
        this->m_currError.reset();
        this->m_evaluator->getConsole().clear();
        this->m_evaluator->setProvider(provider);

        auto preprocessedCode = this->m_preprocessor->preprocess(string);
        if (!preprocessedCode.has_value()) {
            this->m_currError = this->m_preprocessor->getError();
            return { };
        }

        this->m_evaluator->setDefaultEndian(this->m_defaultEndian);
        this->m_evaluator->setRecursionLimit(this->m_recursionLimit);

        auto tokens = this->m_lexer->lex(preprocessedCode.value());
        if (!tokens.has_value()) {
            this->m_currError = this->m_lexer->getError();
            return { };
        }

        auto ast = this->m_parser->parse(tokens.value());
        if (!ast.has_value()) {
            this->m_currError = this->m_parser->getError();
            return { };
        }

        ON_SCOPE_EXIT {
            for(auto &node : ast.value())
                delete node;
        };

        auto validatorResult = this->m_validator->validate(ast.value());
        if (!validatorResult) {
            this->m_currError = this->m_validator->getError();
            return { };
        }

        auto patternData = this->m_evaluator->evaluate(ast.value());
        if (!patternData.has_value())
            return { };

        return patternData.value();
    }

    std::optional<std::vector<PatternData*>> PatternLanguage::executeFile(prv::Provider *provider, const std::string &path) {
        File file(path, File::Mode::Read);

        return this->executeString(provider, file.readString());
    }


    const std::vector<std::pair<LogConsole::Level, std::string>>& PatternLanguage::getConsoleLog() {
        return this->m_evaluator->getConsole().getLog();
    }

    const std::optional<std::pair<u32, std::string>>& PatternLanguage::getError() {
        return this->m_currError;
    }

}