#include "Irc.hpp"

Server::Server(int port, const std::string& pass)
	: _listenPort(port), _connPass(pass), _listenFd(-1) {}

Server::~Server()
{
	for (std::map<int, Client*>::iterator it = _sessions.begin();
		it != _sessions.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
	for (std::map<std::string, Channel*>::iterator it = _rooms.begin();
		it != _rooms.end(); ++it)
		delete it->second;
	if (_listenFd != -1)
		close(_listenFd);
}

void Server::boot()
{
	openSocket();
	std::cout << "Listening on port " << _listenPort << std::endl;
}

void Server::run()
{
	while (g_alive)
	{
		if (poll(&_pollSet[0], _pollSet.size(), 1000) < 0)
		{
			if (!g_alive)
				break;
			throw std::runtime_error("poll() failed");
		}
		std::vector<struct pollfd> snap = _pollSet;
		for (size_t i = 0; i < snap.size(); i++)
		{
			if (snap[i].revents & (POLLHUP | POLLERR))
			{
				if (snap[i].fd != _listenFd
					&& _sessions.find(snap[i].fd) != _sessions.end())
					dropClient(snap[i].fd);
				continue;
			}
			if (snap[i].revents & POLLIN)
			{
				if (snap[i].fd == _listenFd)
					welcomeClient();
				else if (_sessions.find(snap[i].fd) != _sessions.end())
					receiveData(snap[i].fd);
			}
		}
	}
}

// ── Helpers ─────────────────────────────────────────────────────────────────

void Server::transmit(int fd, const std::string& msg)
{
	send(fd, msg.c_str(), msg.size(), 0);
}

void Server::sendNumeric(Client* c, const std::string& code,
	const std::string& target, const std::string& body)
{
	transmit(c->socketFd(),
		":" + hostname() + " " + code + " " + target + " " + body + "\r\n");
}

Client* Server::locateUser(const std::string& nick)
{
	for (std::map<int, Client*>::iterator it = _sessions.begin();
		it != _sessions.end(); ++it)
	{
		if (it->second->getNick() == nick)
			return it->second;
	}
	return NULL;
}

Channel* Server::locateRoom(const std::string& name)
{
	std::map<std::string, Channel*>::iterator it = _rooms.find(name);
	return (it != _rooms.end()) ? it->second : NULL;
}

bool Server::isLegalNick(const std::string& nick)
{
	if (nick.empty() || nick.size() > 9)
		return false;
	if (!std::isalpha(nick[0]) && nick[0] != '_')
		return false;
	for (size_t i = 1; i < nick.size(); i++)
	{
		char c = nick[i];
		if (!std::isalnum(c) && c != '_' && c != '-'
			&& c != '[' && c != ']' && c != '\\'
			&& c != '`' && c != '^' && c != '{' && c != '}')
			return false;
	}
	return true;
}

void Server::listMembers(Client* c, Channel* ch)
{
	std::string names;
	const std::set<Client*>& grp = ch->getUsers();
	for (std::set<Client*>::const_iterator it = grp.begin();
		it != grp.end(); ++it)
	{
		if (!names.empty())
			names += " ";
		if (ch->isModerator(*it))
			names += "@";
		names += (*it)->getNick();
	}
	sendNumeric(c, RPL::NAMREPLY, c->getNick(),
		"= " + ch->getLabel() + " :" + names);
	sendNumeric(c, RPL::ENDOFNAMES, c->getNick(),
		ch->getLabel() + " :End of /NAMES list");
}

void Server::notifyChannels(Client* c, const std::string& msg)
{
	std::set<int> done;
	for (std::map<std::string, Channel*>::iterator it = _rooms.begin();
		it != _rooms.end(); ++it)
	{
		if (!it->second->enrolled(c))
			continue;
		const std::set<Client*>& grp = it->second->getUsers();
		for (std::set<Client*>::const_iterator mi = grp.begin();
			mi != grp.end(); ++mi)
		{
			if (*mi != c && done.find((*mi)->socketFd()) == done.end())
			{
				transmit((*mi)->socketFd(), msg);
				done.insert((*mi)->socketFd());
			}
		}
	}
}

std::string Server::hostname() const { return "ircserv"; }

std::vector<std::string> Server::splitList(const std::string& s, char sep)
{
	std::vector<std::string> out;
	std::istringstream ss(s);
	std::string tok;
	while (std::getline(ss, tok, sep))
	{
		if (!tok.empty())
			out.push_back(tok);
	}
	return out;
}
