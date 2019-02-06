/*
 * determine.h -- the FSM determination routine.
 *
 * Copyright (c) 2007-2010, Dmitry Prokoptsev <dprokoptsev@gmail.com>,
 *                          Alexander Gololobov <agololobov@gmail.com>
 *
 * This file is part of Pire, the Perl Incompatible
 * Regular Expressions library.
 *
 * Pire is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Pire is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser Public License for more details.
 * You should have received a copy of the GNU Lesser Public License
 * along with Pire.  If not, see <http://www.gnu.org/licenses>.
 */


#ifndef PIRE_DETERMINE_H
#define PIRE_DETERMINE_H

#include "stub/stl.h"
#include "partition.h"

namespace Pire {
	namespace Impl {

		/**
		 * An interface of a determination task.
		 * You don't have to derive from this class; it is just a start point template.
		 */
		class DetermineTask {
		private:
			struct ImplementationSpecific1;
			struct ImplementationSpecific2;

		public:
			/// A type representing a new state (may be a set of old states, a pair of them, etc...)
			typedef ImplementationSpecific1 State;

			/// A type of letter equivalence classes table.
			typedef Partition<char, ImplementationSpecific2> LettersTbl;

			/// A container used for storing map of states to thier indices.
			typedef TMap<State, size_t> InvStates;

			/// Should return used letters' partition.
			const LettersTbl& Letters() const;

			/// Should return initial state (surprise!)
			State Initial() const;

			/// Should calculate next state, given the current state and a letter.
			State Next(State state, Char letter) const;

			/// Should return true iff the state need to be processed.
			bool IsRequired(const State& /*state*/) const { return true; }

			/// Called when the set of new states is closed.
			void AcceptStates(const TVector<State>& newstates);

			/// Called for each transition from one new state to another.
			void Connect(size_t from, size_t to, Char letter);

			typedef bool Result;
			Result Success() { return true; }
			Result Failure() { return false; }
		};

		/**
		 * A helper function for FSM determining and all determine-like algorithms
		 * like scanners' agglutination.
		 *
		 * Given an indirectly specified automaton (through Task::Initial() and Task::Next()
		 * functions, see above), performs a breadth-first traversal, finding and enumerating
		 * all effectively reachable states. Then passes all found states and transitions
		 * between them back to the task.
		 *
		 * Initial state is always placed at zero position.
		 *
		 * Please note that the function does not take care of any payload (including final flags);
		 * it is the task's responsibility to agglutinate them properly.
		 *
		 * Returns task.Succeed() if everything was done; task.Failure() if maximum limit of state count was reached.
		 */
		template<class Task>
		typename Task::Result Determine(Task& task, size_t maxSize)
		{
			typedef typename Task::State State;
			typedef typename Task::InvStates InvStates;
			typedef TDeque< TVector<size_t> > TransitionTable;

			TVector<State> states;
			InvStates invstates;
			TransitionTable transitions;
			TVector<size_t> stateIndices;

			states.push_back(task.Initial());
			invstates.insert(typename InvStates::value_type(states[0], 0));

			for (size_t stateIdx = 0; stateIdx < states.size(); ++stateIdx) {
				if (!task.IsRequired(states[stateIdx]))
					continue;
				TransitionTable::value_type row(task.Letters().Size());
				for (auto&& letter : task.Letters()) {
					State newState = task.Next(states[stateIdx], letter.first);
					auto i = invstates.find(newState);
					if (i == invstates.end()) {
						if (!maxSize--)
							return task.Failure();
						i = invstates.insert(typename InvStates::value_type(newState, states.size())).first;
						states.push_back(newState);
					}
					row[letter.second.first] = i->second;
				}
				transitions.push_back(row);
				stateIndices.push_back(stateIdx);
			}

			TVector<Char> invletters(task.Letters().Size());
			for (auto&& letter : task.Letters())
				invletters[letter.second.first] = letter.first;

			task.AcceptStates(states);
			size_t from = 0;
			for (TransitionTable::iterator i = transitions.begin(), ie = transitions.end(); i != ie; ++i, ++from) {
				TVector<Char>::iterator l = invletters.begin();
				for (TransitionTable::value_type::iterator j = i->begin(), je = i->end(); j != je; ++j, ++l)
					task.Connect(stateIndices[from], *j, *l);
			}
			return task.Success();
		}

		// Faster transition table representation for determined FSM
		typedef TVector<size_t> DeterminedTransitions;

		// Mapping of states into partitions in minimization algorithm.
		typedef TVector<size_t> StateClassMap;

		template<class Task>
		struct MinimizeEquality : public ybinary_function<size_t, size_t, bool> {
		public:

			MinimizeEquality(const DeterminedTransitions& tbl, const TVector<Char>& letters, const StateClassMap* clMap, const Task* task) :
				m_tbl(&tbl), m_letters(&letters), m_prev(clMap), m_task(task) {}

			inline bool operator()(size_t a, size_t b) const
			{
				if (m_task) {
					if (!m_task->SameClasses(a, b)) {
						return false;
					}
				}
				if (m_prev) {
					if ((*m_prev)[a] != (*m_prev)[b])
						return false;
					for (auto&& letter : *m_letters)
						if ((*m_prev)[Next(a, letter)] != (*m_prev)[Next(b, letter)])
							return false;
				}
				return true;
			};

		private:
			const DeterminedTransitions* m_tbl;
			const TVector<Char>* m_letters;
			const StateClassMap* m_prev;
			const Task* m_task;

			size_t Next(size_t state, Char letter) const {
				return (*m_tbl)[state * MaxChar + letter];
			}
		};

		// Updates the mapping of states to partitions.
		// Returns true if the mapping has changed.
		// TODO: Think how to update only changed states
		template<class Task>
		bool UpdateStateClassMap(StateClassMap& clMap, const Partition<size_t, MinimizeEquality<Task>>& stPartition)
		{
			Y_ASSERT(!clMap.empty());
			bool changed = false;
			for (size_t st = 0; st < clMap.size(); st++) {
				size_t cl = stPartition.Representative(st);
				if (clMap[st] != cl) {
					clMap[st] = cl;
					changed = true;
				}
			}
			return changed;
		}

		template<class Task>
		typename Task::Result Minimize(Task& task)
		{
			// Minimization algorithm is only applicable to a determined FSM.
			if (!task.IsDetermined()) {
				return task.Failure();
			}

			TVector<Char> distinctLetters;
			DeterminedTransitions detTran(task.Size() * MaxChar);
			for (auto&& letters : task.Letters()) {
				const auto representative = letters.first;
				distinctLetters.push_back(representative);
				for (size_t from = 0; from != task.Size(); ++from) {
					const auto next = task.Next(from, representative);
					for (auto letter : letters.second.second) {
						detTran[from * MaxChar + letter] = next;
					}
				}
			}

			typedef Partition<size_t, MinimizeEquality<Task>> StateClasses;

			StateClasses last(MinimizeEquality<Task>{detTran, distinctLetters, nullptr, &task});

			// Make an initial states partition
			for (size_t state = 0; state < task.Size(); ++state)
				last.Append(state);

			StateClassMap stateClassMap(task.Size());

			// Iteratively split states into equality classes
			while (UpdateStateClassMap(stateClassMap, last)) {
				last.Split(MinimizeEquality<Task>{detTran, distinctLetters, &stateClassMap, nullptr});
			}

			task.AcceptPartition(last);

			return task.Success();
		}
	}
}

#endif
